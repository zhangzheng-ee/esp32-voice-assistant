// ESP32-S3 + GC9A01 圆屏 + MAX98357 : 从 LittleFS 播放 PCM（可暂停/继续）
// 引脚: 屏 SCL=12 SDA=11 DC=9 CS=10 RST=14 ; 功放 BCLK=15 LRC=16 DIN=17
// 分区必须是: ESP SR 16M (3MB APP/7MB SPIFFS/2.9MB MODEL)
// 文件: LittleFS 根目录下 /song.pcm —— 22050Hz / mono / 16bit / 裸PCM(无WAV头)
// 按键: BOOT 按一下开始，再按暂停，再按继续（记住位置）

#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "driver/i2s_std.h"
#include "image.h"

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC   9
#define TFT_CS   10
#define TFT_RST  14

#define I2S_BCLK 15
#define I2S_LRC  16
#define I2S_DIN  17

#define PCM_SR       22050
#define PCM_FILE     "/song.pcm"      // 换歌脚本生成的文件名
#define PCM_FALLBACK "/hello.pcm"     // 没换过歌时播这个

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);
i2s_chan_handle_t tx_handle;

// 播放状态
File    gFile;
bool    gPlaying = false;
size_t  gPos = 0;                 // 暂停时的文件位置（字节）

// 音量：0-100%，数字衰减
int gVolume = 30;
const int VOL_STEPS[] = { 10, 20, 30, 50, 75, 100 };
const int VOL_N = sizeof(VOL_STEPS) / sizeof(VOL_STEPS[0]);
int volIdx = 2;                   // 默认 30%

// ---- 每次喂一小块给 I2S，不阻塞 ----
void streamChunk() {
  static int16_t mono[256];
  static int16_t stereo[512];
  size_t written = 0;

  if (!gFile || !gFile.available()) {
    if (gFile) gFile.close();
    gPlaying = false;
    gPos = 0;
    Serial.println("eof");
    return;
  }

  int n = gFile.read((uint8_t *)mono, sizeof(mono));   // 字节数
  int samples = n / 2;
  if (samples <= 0) return;

  for (int k = 0; k < samples; k++) {                  // mono -> stereo + 音量
    int32_t v = ((int32_t)mono[k] * gVolume) / 100;
    stereo[k * 2]     = (int16_t)v;
    stereo[k * 2 + 1] = (int16_t)v;
  }
  i2s_channel_write(tx_handle, stereo, samples * 4, &written, portMAX_DELAY);
}

// ---- BOOT 键：开始 / 暂停 / 继续 ----
void togglePlay() {
  if (gPlaying) {
    gPos = gFile.position();
    gFile.close();
    gPlaying = false;
    Serial.printf("paused @ %u\n", (unsigned)gPos);
  } else {
    gFile = LittleFS.open(PCM_FILE, "r");
    if (!gFile) gFile = LittleFS.open(PCM_FALLBACK, "r");
    if (!gFile) {
      Serial.println("no pcm file");
      return;
    }
    if (gPos > 0 && gPos < (size_t)gFile.size()) {
      gFile.seek(gPos);
      Serial.printf("resume @ %u\n", (unsigned)gPos);
    } else {
      gPos = 0;
      Serial.println("playing");
    }
    gPlaying = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== boot ===");

  pinMode(0, INPUT_PULLUP);

  // ---- 屏幕 ----
  if (!gfx->begin()) {
    Serial.println("gfx->begin FAILED");
    while (1) delay(1000);
  }
  gfx->fillScreen(0x0000);
  gfx->draw16bitRGBBitmap(0, 0, image_data, IMG_W, IMG_H);

  // ---- LittleFS ----
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount FAILED -- 分区方案选的是 ESP SR 16M 吗?");
    if (!LittleFS.begin(true)) {
      Serial.println("format also failed");
    } else {
      Serial.println("已格式化，镜像没了，要重烧 littlefs.bin");
    }
  } else {
    Serial.printf("LittleFS OK, 已用 %u / 总 %u 字节\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    File root = LittleFS.open("/");
    File e = root.openNextFile();
    while (e) {
      Serial.printf("  %-20s %u bytes\n", e.name(), (unsigned)e.size());
      e = root.openNextFile();
    }
    root.close();
  }

  // ---- I2S ----
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PCM_SR),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

  Serial.printf("ready @ %d Hz, BOOT = play / pause / resume\n", PCM_SR);
}

void loop() {
  // 按键：短按 = 播放/暂停，长按(>0.8s) = 切音量
  static bool          pressed = false;
  static unsigned long pressStart = 0;

  bool key = digitalRead(0);

  if (key == LOW && !pressed) {
    pressed = true;
    pressStart = millis();
  } else if (key == HIGH && pressed) {
    pressed = false;
    unsigned long dur = millis() - pressStart;
    if (dur > 800) {
      volIdx = (volIdx + 1) % VOL_N;
      gVolume = VOL_STEPS[volIdx];
      Serial.printf("volume %d%%\n", gVolume);
    } else if (dur > 30) {
      togglePlay();
    }
  }

  if (gPlaying) {
    streamChunk();
  } else {
    delay(5);
  }
}