# Turns the design's icons and logos into LVGL 9 C images (src/ui/images/).
#
#   powershell -ExecutionPolicy Bypass -File tools\make_images.ps1
#
# SVGs (exported from the Figma file into Design/export/) are drawn at the exact
# pixel size the screen uses, so icons stay crisp. PNGs are resized if needed.
# Output is ARGB8888 with straight alpha, which LVGL blends onto any background.
# Edit the $images list to add or resize an image, then run this again.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $root 'src\ui\images'

#            name            source (repo-relative)                   width height
$images = @(
  @('logo_avtf',       'Design\AVTF_BESS_v10.png',                  100, 25),
  @('logo_ncca',       'Design\NCCA Logo.png',                       62, 62),
  @('arrow_title',     'Design\export\arrow.svg',                    10, 30),
  @('arrow_small',     'Design\export\arrow_small.svg',               5, 16),
  @('chevron',         'Design\export\chevron.svg',                  18,  9),
  @('ic_cart',         'Design\export\simple_cart.svg',              20, 20),
  @('ic_bt_small',     'Design\export\bluetooth_tag_solid_29.svg',   14, 14),
  @('ic_wifi_small',   'Design\export\wifi_tag_solid.svg',           14, 14),
  @('ic_server_small', 'Design\export\server_connection_solid_30.svg', 14, 14),
  @('ic_bt',           'Design\export\bluetooth_tag_solid_29.svg',   29, 29),
  @('ic_wifi',         'Design\export\wifi_tag_solid_26.svg',        26, 24),
  @('ic_server',       'Design\export\server_connection_solid_30.svg', 29, 29),
  @('ic_ram',          'Design\export\dashboard_speed_ram.svg',      16, 15),
  @('ic_psram',        'Design\export\dashboard_speed_psram.svg',    16, 15),
  @('ic_clock',        'Design\export\clock_solid.svg',              16, 16),
  @('ic_temp',         'Design\export\temperature_high.svg',         16, 17)
)

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;

public static class LvImage {
  static float F(string s) { return float.Parse(s, CultureInfo.InvariantCulture); }

  static string Attr(string attrs, string name) {
    Match m = Regex.Match(attrs, "(?:^|\\s)" + Regex.Escape(name) + "=\"([^\"]*)\"");
    return m.Success ? m.Groups[1].Value : null;
  }

  static Color Hex(string s) { return ColorTranslator.FromHtml(s); }

  // SVG path data: M L H V C S Z, absolute or relative (what Figma exports).
  static GraphicsPath ParsePath(string d, FillMode mode) {
    GraphicsPath path = new GraphicsPath(mode);
    MatchCollection tok = Regex.Matches(d, "[MmLlHhVvCcSsZz]|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?");
    int i = 0;
    char cmd = 'M';
    PointF cur = new PointF(0, 0), start = cur, ctrl = cur;
    while (i < tok.Count) {
      string t = tok[i].Value;
      if (char.IsLetter(t[0])) {
        cmd = t[0];
        i++;
        if (cmd == 'Z' || cmd == 'z') { path.CloseFigure(); cur = start; ctrl = cur; continue; }
      }
      bool rel = char.IsLower(cmd);
      float ox = rel ? cur.X : 0, oy = rel ? cur.Y : 0;
      switch (char.ToUpper(cmd)) {
        case 'M': {
          PointF p = new PointF(F(tok[i].Value) + ox, F(tok[i + 1].Value) + oy); i += 2;
          path.StartFigure(); cur = start = ctrl = p;
          cmd = rel ? 'l' : 'L';                       // more pairs after M are lines
          break;
        }
        case 'L': {
          PointF p = new PointF(F(tok[i].Value) + ox, F(tok[i + 1].Value) + oy); i += 2;
          path.AddLine(cur, p); cur = ctrl = p; break;
        }
        case 'H': { PointF p = new PointF(F(tok[i].Value) + ox, cur.Y); i += 1; path.AddLine(cur, p); cur = ctrl = p; break; }
        case 'V': { PointF p = new PointF(cur.X, F(tok[i].Value) + oy); i += 1; path.AddLine(cur, p); cur = ctrl = p; break; }
        case 'C': {
          PointF c1 = new PointF(F(tok[i].Value) + ox, F(tok[i + 1].Value) + oy);
          PointF c2 = new PointF(F(tok[i + 2].Value) + ox, F(tok[i + 3].Value) + oy);
          PointF p  = new PointF(F(tok[i + 4].Value) + ox, F(tok[i + 5].Value) + oy); i += 6;
          path.AddBezier(cur, c1, c2, p); ctrl = c2; cur = p; break;
        }
        case 'S': {
          PointF c1 = new PointF(2 * cur.X - ctrl.X, 2 * cur.Y - ctrl.Y);
          PointF c2 = new PointF(F(tok[i].Value) + ox, F(tok[i + 1].Value) + oy);
          PointF p  = new PointF(F(tok[i + 2].Value) + ox, F(tok[i + 3].Value) + oy); i += 4;
          path.AddBezier(cur, c1, c2, p); ctrl = c2; cur = p; break;
        }
        default: throw new Exception("Unsupported SVG path command: " + cmd);
      }
    }
    return path;
  }

  public static Bitmap RenderSvg(string file, int w, int h) {
    string svg = Regex.Replace(File.ReadAllText(file), "<defs>.*?</defs>", "", RegexOptions.Singleline);
    string[] vb = Attr(svg, "viewBox").Split(new char[] { ' ', ',' }, StringSplitOptions.RemoveEmptyEntries);
    Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
    using (Graphics g = Graphics.FromImage(bmp)) {
      g.SmoothingMode = SmoothingMode.AntiAlias;
      g.PixelOffsetMode = PixelOffsetMode.HighQuality;
      g.Clear(Color.Transparent);
      g.ScaleTransform(w / F(vb[2]), h / F(vb[3]));
      g.TranslateTransform(-F(vb[0]), -F(vb[1]));
      foreach (Match m in Regex.Matches(svg, "<path\\b([^>]*)>")) {
        string a = m.Groups[1].Value;
        string d = Attr(a, "d");
        if (d == null) continue;
        FillMode mode = Attr(a, "fill-rule") == "evenodd" ? FillMode.Alternate : FillMode.Winding;
        using (GraphicsPath p = ParsePath(d, mode)) {
          string fill = Attr(a, "fill"), stroke = Attr(a, "stroke");
          if (fill != null && fill != "none")
            using (SolidBrush br = new SolidBrush(Hex(fill))) g.FillPath(br, p);
          if (stroke != null && stroke != "none") {
            using (Pen pen = new Pen(Hex(stroke), F(Attr(a, "stroke-width") ?? "1"))) {
              if (Attr(a, "stroke-linecap") == "round") { pen.StartCap = LineCap.Round; pen.EndCap = LineCap.Round; }
              if (Attr(a, "stroke-linejoin") == "round") pen.LineJoin = LineJoin.Round;
              g.DrawPath(pen, p);
            }
          }
        }
      }
    }
    return bmp;
  }

  public static Bitmap LoadPng(string file, int w, int h) {
    using (Bitmap src = new Bitmap(file)) {
      Bitmap bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
      using (Graphics g = Graphics.FromImage(bmp)) {
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        g.Clear(Color.Transparent);
        g.DrawImage(src, new Rectangle(0, 0, w, h));
      }
      return bmp;
    }
  }

  // LVGL ARGB8888 is B,G,R,A in memory, which is exactly GDI+'s 32bppArgb layout.
  public static void WriteC(Bitmap bmp, string name, string path, string source) {
    int w = bmp.Width, h = bmp.Height;
    BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    byte[] px = new byte[w * h * 4];
    for (int y = 0; y < h; y++) Marshal.Copy(bd.Scan0 + y * bd.Stride, px, y * w * 4, w * 4);
    bmp.UnlockBits(bd);

    StringBuilder sb = new StringBuilder();
    sb.Append("// Generated by tools/make_images.ps1 from ").Append(source.Replace('\\', '/')).Append(". Don't edit.\n");
    sb.Append("#include \"lvgl.h\"\n\n");
    sb.Append("static const uint8_t ").Append(name).Append("_map[] = {\n");
    for (int i = 0; i < px.Length; i++) {
      if (i % 16 == 0) sb.Append("  ");
      sb.Append("0x").Append(px[i].ToString("x2")).Append(',');
      sb.Append(i % 16 == 15 || i == px.Length - 1 ? "\n" : "");
    }
    sb.Append("};\n\n");
    sb.Append("const lv_image_dsc_t ").Append(name).Append(" = {\n");
    sb.Append("  .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = ")
      .Append(w).Append(", .h = ").Append(h).Append(", .stride = ").Append(w * 4).Append("},\n");
    sb.Append("  .data_size = sizeof(").Append(name).Append("_map),\n");
    sb.Append("  .data = ").Append(name).Append("_map,\n};\n");
    File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
  }
}
'@

New-Item -ItemType Directory -Force $out | Out-Null
$decls = @()
foreach ($img in $images) {
  $name, $src, $w, $h = $img
  $file = Join-Path $root $src
  $bmp = if ($src.EndsWith('.svg')) { [LvImage]::RenderSvg($file, $w, $h) } else { [LvImage]::LoadPng($file, $w, $h) }
  [LvImage]::WriteC($bmp, "img_$name", (Join-Path $out "img_$name.c"), $src)
  $bmp.Save((Join-Path $env:TEMP "img_$name.png"))   # preview, not committed
  $bmp.Dispose()
  $decls += "LV_IMAGE_DECLARE(img_$name);"
  "  img_$name  ${w}x$h  <- $src"
}

$header = @"
#pragma once
// Generated by tools/make_images.ps1. Don't edit; change the list in that script and run it.
#include <lvgl.h>

$($decls -join "`n")
"@
[IO.File]::WriteAllText((Join-Path $out 'Images.h'), $header.Replace("`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding $false))
"Wrote $($images.Count) images to src\ui\images"
