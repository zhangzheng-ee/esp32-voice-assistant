// 待机程序：只显示，不录音不出声
#include <Arduino_GFX_Library.h>

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC   9
#define TFT_CS   10
#define TFT_RST  14

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== idle ===");
  if (!gfx->begin()) { Serial.println("gfx fail"); return; }
  gfx->fillScreen(0x0000);
  // 中心画个圈，表示待机
  gfx->drawCircle(120, 120, 80, 0x07E0);
  gfx->drawCircle(120, 120, 60, 0x07E0);
  Serial.println("idle ready");
}

void loop() { delay(2000); }