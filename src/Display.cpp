#include "Display.h"
#include "config.h"
#include <Arduino.h>
#include <lvgl.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace {
// ---- Panel + touch driver ------------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Touch_FT5x06  _touch;   // FT6336U speaks the FT5x06 protocol

 public:
  LGFX() {
    {
      auto cfg        = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = TFT_SPI_HZ;
      cfg.freq_read   = 16000000;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_TFT_SCLK;
      cfg.pin_mosi    = PIN_TFT_MOSI;
      cfg.pin_miso    = PIN_TFT_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg         = _panel.config();
      cfg.pin_cs       = PIN_TFT_CS;
      cfg.pin_rst      = PIN_TFT_RST;
      cfg.pin_busy     = -1;
      cfg.panel_width  = 320;
      cfg.panel_height = 480;
      cfg.readable     = true;
      cfg.invert       = false;   // colors inverted? flip this
      cfg.rgb_order    = false;   // red and blue swapped? flip this
      _panel.config(cfg);
    }
    {
      auto cfg     = _touch.config();
      cfg.i2c_port = 0;
      cfg.pin_sda  = PIN_TOUCH_SDA;
      cfg.pin_scl  = PIN_TOUCH_SCL;
      cfg.pin_int  = PIN_TOUCH_INT;
      cfg.i2c_addr = TOUCH_I2C_ADDR;
      cfg.freq     = 400000;
      cfg.x_min = 0;  cfg.x_max = 319;
      cfg.y_min = 0;  cfg.y_max = 479;
      cfg.offset_rotation = 0;    // taps land in the wrong spot? try 1, 2 or 3
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX tft;

constexpr int32_t  SCREEN_W  = 480;
constexpr int32_t  SCREEN_H  = 320;
constexpr uint32_t BUF_LINES = 40;
constexpr size_t   BUF_BYTES = SCREEN_W * BUF_LINES * sizeof(uint16_t);   // 38,400 bytes each

// ---- LVGL <-> hardware glue ----------------------------------------------------
// LVGL renders a strip into one buffer; DMA ships it over SPI while LVGL draws the next.
void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(px, w * h);                 // SPI panels want big-endian pixels
  tft.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<lgfx::swap565_t*>(px));
  lv_display_flush_ready(disp);                      // safe: two buffers, and DMA waits for the previous one
}

void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  int32_t x, y;
  if (tft.getTouch(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

uint32_t tickCb() { return millis(); }
}  // namespace

bool Display::begin() {
  if (!psramFound()) {
    Serial.println("[display] PSRAM not found. Check memory_type = qio_opi in platformio.ini");
    return false;
  }
  if (PIN_TFT_BL >= 0) {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
  }
  tft.init();
  tft.setRotation(1);                                // landscape, 480x320
  tft.fillScreen(TFT_BLACK);
  tft.startWrite();                                  // keep the SPI bus for DMA

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(SCREEN_W, SCREEN_H);
  void* buf1 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  lv_display_set_buffers(disp, buf1, buf2, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touchCb);

  Serial.println("[display] ready");
  return true;
}

void Display::tick() { lv_timer_handler(); }
