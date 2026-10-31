# UI simulator: runs the station's real screens (src/Ui.cpp, src/ui/, src/Inventory.cpp)
# on this PC, taps and scans through the main flows, and checks what happens.
#
#   powershell -ExecutionPolicy Bypass -File tools\ui-sim\run.ps1
#
# Needs MinGW (g++ and mingw32-make; C:\MinGW\bin is used if they're not on PATH).
# Downloads LVGL 9.2.2 and ArduinoJson 7.4.3 (the versions in platformio.ini) into
# tools\.cache the first time. Screenshots land in tools\ui-sim\out, and each
# compare_*.png puts a Figma screen (left) next to the station's version (right).
$ErrorActionPreference = 'Stop'
$root  = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cache = Join-Path $root 'tools\.cache\ui-sim'
$out   = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force "$cache\obj", "$cache\include", $out | Out-Null
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) { $env:PATH = "C:\MinGW\bin;$env:PATH" }

# ---- Dependencies, pinned like platformio.ini ---------------------------------------
function Fetch($url, $file) {
  & "$env:SystemRoot\System32\curl.exe" -sSfL -o $file $url   # Windows' own, even when run from Git Bash
  if ($LASTEXITCODE -ne 0) { throw "Download failed: $url" }
}
$lvgl = Join-Path $cache 'lvgl-9.2.2'
if (-not (Test-Path $lvgl)) {                       # unpacked under another name, renamed once complete
  Fetch 'https://github.com/lvgl/lvgl/archive/refs/tags/v9.2.2.tar.gz' "$cache\lvgl.tar.gz"
  Remove-Item -Recurse -Force "$cache\unpack" -ErrorAction SilentlyContinue
  New-Item -ItemType Directory "$cache\unpack" | Out-Null
  & "$env:SystemRoot\System32\tar.exe" -xzf "$cache\lvgl.tar.gz" -C "$cache\unpack" lvgl-9.2.2/lvgl.h lvgl-9.2.2/lv_version.h lvgl-9.2.2/src
  if ($LASTEXITCODE -ne 0) { throw 'Unpacking LVGL failed' }
  Move-Item "$cache\unpack\lvgl-9.2.2" $lvgl
  Remove-Item -Recurse -Force "$cache\unpack", "$cache\lvgl.tar.gz"
}
if (-not (Test-Path "$cache\include\ArduinoJson.h")) {
  Fetch 'https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.3/ArduinoJson-v7.4.3.h' "$cache\include\ArduinoJson.h"
}

# ---- Makefile: LVGL objects are built once; the app rebuilds when any header changes --
function Rel($path) { $path.Substring($root.Length + 1).Replace('\', '/') }
function Obj($src) { 'tools/.cache/ui-sim/obj/' + ($src -replace '[/.]', '_') + '.o' }

$lvglSrc = Get-ChildItem "$lvgl\src" -Recurse -Filter *.c | ForEach-Object { Rel $_.FullName }
$appSrc  = @('src/Inventory.cpp', 'src/Ui.cpp', 'tools/ui-sim/main.cpp', 'tools/ui-sim/fakes.cpp') +
           (Get-ChildItem "$root\src\ui" -Recurse -Include *.cpp, *.c | ForEach-Object { Rel $_.FullName })
$headers = Get-ChildItem "$root\src", "$root\include", "$root\tools\ui-sim" -Recurse -Include *.h |
           ForEach-Object { Rel $_.FullName }

$inc  = '-DLV_CONF_INCLUDE_SIMPLE -Itools/ui-sim/stubs -Itools/.cache/ui-sim/lvgl-9.2.2'
$mk   = @(
  "CFLAGS = -O1 -std=gnu99 $inc",
  "CXXFLAGS = -O1 -std=gnu++1z -Wall -Wno-misleading-indentation $inc -Itools/.cache/ui-sim/include -Isrc -Iinclude -Itools/ui-sim",
  "HEADERS = $($headers -join ' ')",
  "SIM = tools/.cache/ui-sim/sim.exe",
  "all: `$(SIM)")
$objs = @()
foreach ($s in $lvglSrc) { $o = Obj $s; $objs += $o; $mk += "${o}: $s"; $mk += "`tgcc `$(CFLAGS) -c $s -o $o" }
foreach ($s in $appSrc) {
  $o = Obj $s; $objs += $o
  $mk += "${o}: $s `$(HEADERS)"
  $mk += if ($s.EndsWith('.c')) { "`tgcc `$(CFLAGS) -c $s -o $o" } else { "`tg++ `$(CXXFLAGS) -c $s -o $o" }
}
$mk += "`$(SIM): $($objs -join ' ')"
$mk += "`tg++ -o `$(SIM) $($objs -join ' ')"
[IO.File]::WriteAllText("$cache\Makefile", ($mk -join "`n") + "`n")

Push-Location $root
try {
  & mingw32-make -s -j $env:NUMBER_OF_PROCESSORS -f tools/.cache/ui-sim/Makefile
  if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
  Remove-Item "$out\*" -ErrorAction SilentlyContinue
  & "$cache\sim.exe" $out
  $testExit = $LASTEXITCODE
} finally { Pop-Location }

# ---- Screenshots: BMP -> PNG, and Figma vs station side by side ------------------------
Add-Type -AssemblyName System.Drawing
Get-ChildItem "$out\*.bmp" | ForEach-Object {
  $img = [Drawing.Image]::FromFile($_.FullName)
  $img.Save(($_.FullName -replace '\.bmp$', '.png'), [Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  Remove-Item $_.FullName
}
$pairs = [ordered]@{
  '1_sign_on'   = 'Sign On Screen.png'
  '2_check_in'  = 'Check In Screen.png'
  '3_check_out' = 'Check Out Screen.png'
  '4_pin'       = 'Settings Screen-1.png'
  '5_settings'  = 'Settings Screen.png'
  '6_bluetooth' = 'Settings Screen - Bluetooth Options.png'
  '7_wifi'      = 'Settings Screen - Bluetooth Options-1.png'
}
$font = New-Object Drawing.Font('Segoe UI', 10)
foreach ($name in $pairs.Keys) {
  $simPng = "$out\$name.png"
  if (-not (Test-Path $simPng)) { continue }
  $sheet = New-Object Drawing.Bitmap 972, 344
  $g = [Drawing.Graphics]::FromImage($sheet)
  $g.Clear([Drawing.Color]::FromArgb(40, 40, 40))
  $design = [Drawing.Image]::FromFile((Join-Path $root "Design\$($pairs[$name])"))
  $station = [Drawing.Image]::FromFile($simPng)
  $g.DrawImage($design, 4, 20, 480, 320)
  $g.DrawImage($station, 488, 20, 480, 320)
  $g.DrawString("Figma: $($pairs[$name])", $font, [Drawing.Brushes]::White, 4, 2)
  $g.DrawString("Station: $name", $font, [Drawing.Brushes]::White, 488, 2)
  $design.Dispose(); $station.Dispose(); $g.Dispose()
  $sheet.Save("$out\compare_$name.png", [Drawing.Imaging.ImageFormat]::Png)
  $sheet.Dispose()
}
"Screenshots and comparisons: $out"
exit $testExit
