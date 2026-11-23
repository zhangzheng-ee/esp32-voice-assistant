// INMP441 v17 —— 屏幕提示录音窗口（白=正在录，黑=在传输）
// 接线: 屏 12/11/9/10/14 | 麦 VDD->3V3 GND->GND L/R->GND SCK->4 WS->5 SD->6

#include <Arduino_GFX_Library.h>
#include "driver/i2s_std.h"
#include <math.h>

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC   9
#define TFT_CS   10
#define TFT_RST  14

#define MIC_SCK 4
#define MIC_WS  5
#define MIC_SD  6
#define MIC_SR  8000
#define REC_SEC 3
#define REC_N   (MIC_SR * REC_SEC)
#define GAIN    30
#define HP_FC   100.0f

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);

i2s_chan_handle_t rx;
static int16_t gBuf[REC_N];
int gRound = 0;
float gA = 0.0f, gX1 = 0.0f, gY1 = 0.0f;

static inline int16_t hp(float x) {
  float y = gA * (gY1 + x - gX1);
  gX1 = x; gY1 = y;
  int32_t s = (int32_t)(y * GAIN);
  if (s >  32767) s =  32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

void setup() {
  Serial.begin(115200);
  delay(600);

  gfx->begin();
  gfx->fillScreen(0x0000);

  float rc = 1.0f / (2.0f * PI * HP_FC);
  float dt = 1.0f / MIC_SR;
  gA = rc / (rc + dt);
  Serial.printf("=== v17 fs=%d GAIN=%d ===\n", MIC_SR, GAIN);

  i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cfg.dma_desc_num  = 8;
  cfg.dma_frame_num = 256;
  ESP_ERROR_CHECK(i2s_new_channel(&cfg, NULL, &rx));

  i2s_std_config_t std = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)MIC_SCK,
      .ws   = (gpio_num_t)MIC_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)MIC_SD,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std));
  ESP_ERROR_CHECK(i2s_channel_enable(rx));

  int32_t tmp[512]; size_t got;
  for (int i = 0; i < MIC_SR / 128; i++)
    i2s_channel_read(rx, tmp, sizeof(tmp), &got, 1000);

  Serial.println("READY - 白屏时说，黑屏时等");
}

void loop() {
  static int32_t raw[512];
  int n = 0; size_t got = 0;

  gfx->fillScreen(0xFFFF);   // 白 = 正在录

  while (n < REC_N) {
    if (i2s_channel_read(rx, raw, sizeof(raw), &got, 1000) != ESP_OK) break;
    int cnt = got / 4;
    for (int i = 0; i < cnt && n < REC_N; i += 2) {
      int32_t v = raw[i] >> 8;
      gBuf[n++] = hp((float)(v >> 8));
    }
  }

  gfx->fillScreen(0x0000);   // 黑 = 正在传

  float s2 = 0; int peak = 0, clip = 0;
  for (int i = 0; i < n; i++) {
    s2 += (float)gBuf[i] * gBuf[i];
    if (abs(gBuf[i]) > abs(peak)) peak = gBuf[i];
    if (gBuf[i] >= 32767 || gBuf[i] <= -32768) clip++;
  }
  Serial.printf("STAT round=%d n=%d peak=%d rms=%.0f clip=%d\n",
                gRound, n, peak, n ? sqrtf(s2/n) : 0, clip);

  Serial.println("===REC_START===");
  for (int i = 0; i < n; i++) Serial.printf("%04X\n", (uint16_t)gBuf[i]);
  Serial.println("===REC_END===");

  gRound++;
  delay(800);
}