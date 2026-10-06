#pragma once
// ============================================================================
//  Everything you're expected to edit lives in this one file.
//  Don't commit real Wi-Fi passwords or the API token to a public repo.
// ============================================================================
#include <stdint.h>

// ---- Wi-Fi + Google Apps Script ---------------------------------------------
// WIFI_SSID / WIFI_PASS = the default network. Pick another one on System > Wi-Fi and
// the station saves it and uses it from then on; Forget there brings this one back.
// SCRIPT_URL = the STATION script's /exec URL (personal Gmail, Who has access: Anyone).
// A school-account URL won't work here: the station can't sign in to Google, and the
// System tab will say "needs public deployment".
constexpr const char* WIFI_SSID  = "Guest";          // 2.4 GHz only
constexpr const char* WIFI_PASS  = "";
constexpr const char* SCRIPT_URL = "https://script.google.com/macros/s/AKfycbwF5KFPHNB6SFxTc85XYyphoPIbN-hRJtV7aeuvit7ZScbYaUgjaNbEEPGHsj4i7o_K/exec";
constexpr const char* API_TOKEN  = "jdiwnu8329rejdowhe92ueiowpqu9e01ue89yru23yy46782ryeho2u8y7382yr7ewy";  // must match TOKEN in Code.gs

// ---- Timing -----------------------------------------------------------------
constexpr uint32_t SYNC_INTERVAL_MS      = 5 * 60 * 1000;  // pull roster + items from Sheets
constexpr uint32_t METRICS_INTERVAL_MS   = 60 * 1000;      // push chip health to Sheets
constexpr uint32_t UPLOAD_RETRY_MS       = 5 * 1000;       // wait after a failed upload
constexpr uint32_t CONFIG_ERROR_RETRY_MS = 60 * 1000;      // slower retry for setup problems (wrong token, not public)
constexpr uint32_t WIFI_RETRY_MS         = 10 * 1000;      // reconnect attempts when Wi-Fi drops
constexpr uint32_t WIFI_JOIN_TIMEOUT_MS  = 15 * 1000;      // give up on one try at joining a network
constexpr uint32_t SCANNER_RETRY_MS      = 10 * 1000;      // reconnect attempts to the saved scanner
constexpr uint32_t DISCOVERY_DURATION_MS = 5 * 1000;       // how long "Find scanners" searches

// ---- Display: ST7796S over SPI (match your wiring) ---------------------------
constexpr int PIN_TFT_SCLK = 12;
constexpr int PIN_TFT_MOSI = 11;
constexpr int PIN_TFT_MISO = 13;
constexpr int PIN_TFT_DC   = 9;
constexpr int PIN_TFT_CS   = 10;
constexpr int PIN_TFT_RST  = 8;
constexpr int PIN_TFT_BL   = -1;           // backlight pin, or -1 if it's wired to 3.3V
constexpr uint32_t TFT_SPI_HZ = 40000000;  // try 80 MHz once wiring is short and solid

// ---- Touch: FT6336U over I2C (match your wiring) -----------------------------
constexpr int PIN_TOUCH_SDA = 6;
constexpr int PIN_TOUCH_SCL = 7;
constexpr int PIN_TOUCH_INT = 5;           // -1 if not connected
constexpr uint8_t TOUCH_I2C_ADDR = 0x38;   // FT6336U. GT911 boards use 0x5D + a different driver
