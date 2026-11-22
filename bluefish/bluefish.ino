/*
 * ESP32-S3 语音助手终端（不依赖任何第三方库）
 * ---------------------------------------------------------------------------
 * 为什么有这个版本：Arduino 的库管理器在你那边搜不到 WebSockets / ArduinoJson。
 * 这版只用 ESP32 自带的东西（WiFi + WiFiClient + I2S），一个库都不用装。
 *
 * 接线（和你那套一致，按丝印/物理位置，不看颜色）：
 *
 *   INMP441 麦克风        ESP32-S3        1.28" 圆屏 ST7789（可选）   ESP32-S3
 *     VDD ── 3V3                            VCC ── 3V3
 *     GND ── GND                            GND ── GND
 *     L/R ── GND                            SCL ── GPIO12
 *     SCK ── GPIO4                          SDA ── GPIO11
 *     WS  ── GPIO5                          RES ── GPIO9
 *     SD  ── GPIO6                          DC  ── GPIO8
 *                                           CS  ── GPIO10
 *   MAX98357 功放         ESP32-S3          背光 BLO ── 3V3
 *     VIN ── 5V  ← 必须 5V
 *     GND ── GND
 *     LRC ── GPIO16
 *     BCLK ─ GPIO15
 *     DIN ── GPIO17
 *
 *   ⚠️ GPIO33~37 被 8MB PSRAM 占着，别往那儿接。
 *   ⚠️ 屏幕这版没用上（你原来那张 sketch 已经有屏了），留着引脚注释方便以后合。
 *
 * 用法：按住 BOOT 说话，松手 → 她听懂 → 她的话从喇叭里出来。
 *
 * 板子设置（Arduino IDE）：ESP32S3 Dev Module / Flash 16MB / PSRAM OPI PSRAM
 */

#include <WiFi.h>
#include "driver/i2s_std.h"
#include <WiFiClient.h>
#include <driver/i2s.h>

// ---------------------------------------------------------------- 改这里
const char *WIFI_SSID = "YOUR_WIFI_SSID";           // iPhone 热点（开"最大兼容性"= 2.4GHz）
const char *WIFI_PASS = "YOUR_WIFI_PASSWORD";
#include "config.h"   // 网络与设备令牌；本文件不入库，模板见 config.example.h
// -------------------------------------------------------------------------

#define PIN_MIC_SCK 4
#define PIN_MIC_WS 5
#define PIN_MIC_SD 6
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC 16
#define PIN_SPK_DIN 17
#define PIN_BOOT 0

#define SR 16000
#define MAX_REC_SEC 25
#define MAX_REC_BYTES (MAX_REC_SEC * SR * 2)
#define I2S_MIC I2S_NUM_0
#define I2S_SPK I2S_NUM_1

int16_t *recBuf = nullptr;      // PSRAM 里的录音缓冲
size_t recBytes = 0;
bool recording = false;
unsigned long recStart = 0;
WiFiClient net;

i2s_chan_handle_t micHandle = NULL;

// 32bit STEREO：BCLK = 64×fs，符合 INMP441 标准 I2S 从机时序
// 读出来是 int32[L,R,L,R...]，左声道有效，>>16 得 16bit
void setupI2sMic() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 6;
  chan_cfg.dma_frame_num = 240;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &micHandle));

  i2s_std_config_t std_cfg = {};
  std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SR);
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.bclk = (gpio_num_t)PIN_MIC_SCK;
  std_cfg.gpio_cfg.ws   = (gpio_num_t)PIN_MIC_WS;
  std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.din  = (gpio_num_t)PIN_MIC_SD;
  std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(micHandle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(micHandle));
  Serial.println("[mic] 新API(i2s_std) 32bit STEREO 初始化完成");
}

i2s_chan_handle_t spkHandle = NULL;

void setupI2sSpk() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 6;
  chan_cfg.dma_frame_num = 240;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &spkHandle, NULL));

  i2s_std_config_t std_cfg = {};
  std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SR);
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.bclk = (gpio_num_t)PIN_SPK_BCLK;
  std_cfg.gpio_cfg.ws   = (gpio_num_t)PIN_SPK_LRC;
  std_cfg.gpio_cfg.dout = (gpio_num_t)PIN_SPK_DIN;
  std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(spkHandle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(spkHandle));
  Serial.println("[spk] 新API(i2s_std) 初始化完成");
}

// ------------------------------------------------------------------ 网络小工具

bool connectServer() {
  if (net.connected()) return true;
  Serial.printf("[net] 连 %s:%u …\n", SERVER_HOST, SERVER_PORT);
  net.stop();
  if (!net.connect(SERVER_HOST, SERVER_PORT, 4000)) {
    Serial.println("[net] 连不上（检查热点频段是不是 2.4GHz、电脑 IP 对不对）");
    return false;
  }
  net.setTimeout(60);
  Serial.println("[net] TCP 通了");
  return true;
}

/** 读到 \r\n\r\n 为止，返回状态码；同时把 Content-Length 取出来 */
int readHttpHead(WiFiClient &c, long *contentLength, String *xText) {
  *contentLength = -1;
  String line = "";
  int status = -1;
  bool first = true;
  unsigned long t0 = millis();
  while (millis() - t0 < 25000) {
    if (!c.available()) { delay(5); continue; }
    char ch = (char)c.read();
    if (ch == '\r') continue;
    if (ch != '\n') { line += ch; continue; }
    if (first) {
      int sp = line.indexOf(' ');
      if (sp > 0) status = line.substring(sp + 1, sp + 4).toInt();
      first = false;
    } else if (line.length() == 0) {
      return status;   // 空行 = 头结束
    } else {
      int colon = line.indexOf(':');
      if (colon > 0) {
        String k = line.substring(0, colon);
        String v = line.substring(colon + 1);
        v.trim();
        k.toLowerCase();
        if (k == "content-length") *contentLength = v.toInt();
        if (k == "x-text" && xText) *xText = v;
      }
    }
    line = "";
  }
  return status;
}

/** 把录音 POST 上去，返回她听到的文字（服务器回的纯文本） */
String postUtter(const uint8_t *data, size_t len) {
  if (!connectServer()) return "";
  String path = String("/utter?token=") + DEVICE_TOKEN;
  net.printf("POST %s HTTP/1.1\r\n", path.c_str());
  net.printf("Host: %s:%u\r\n", SERVER_HOST, SERVER_PORT);
  net.print("Content-Type: application/octet-stream\r\n");
  net.printf("Content-Length: %u\r\n", (unsigned)len);
  net.print("Connection: keep-alive\r\n\r\n");
  // 分块写，别一次 printf 835KB
  size_t sent = 0;
  size_t chunk = 4096;
  while (sent < len) {
    size_t n = (len - sent > chunk) ? chunk : (len - sent);
    size_t w = net.write(data + sent, n);
    if (w == 0) { Serial.println("[net] 上传断了"); net.stop(); return ""; }
    sent += w;
  }
  long cl = -1;
  int st = readHttpHead(net, &cl, nullptr);
  Serial.printf("[net] /utter → %d\n", st);
  String body = "";
  if (cl > 0) {
    unsigned long t0 = millis();
    while ((long)body.length() < cl && millis() - t0 < 20000) {
      while (net.available()) body += (char)net.read();
      if (!net.available()) delay(10);
    }
  }
  body.trim();
  return body;
}

/** 长轮询等她说的话，收到的 PCM 直接边收边放 */
void pullAndPlay(long waitMs) {
  net.stop();          // 强制新建连接，避免 /utter 的残留响应污染 /say
  delay(60);
  if (!connectServer()) return;
  String path = String("/say?token=") + DEVICE_TOKEN + "&wait=" + String(waitMs);
  net.printf("GET %s HTTP/1.1\r\n", path.c_str());
  net.printf("Host: %s:%u\r\n", SERVER_HOST, SERVER_PORT);
  net.print("Connection: close\r\n\r\n");
  long cl = -1;
  String xText;
  int st = readHttpHead(net, &cl, &xText);
  if (st == 204) { Serial.println("[net] 她暂时没说话"); net.stop(); return; }
  if (st != 200) { Serial.printf("[net] /say → %d\n", st); net.stop(); return; }
  Serial.printf("[她] %s（%ld 字节）\n", xText.c_str(), cl);


  static uint8_t buf[3200];
  long got = 0;
  unsigned long t0 = millis();
  while (cl < 0 || got < cl) {
    if (cl >= 0 && millis() - t0 > 60000) break;
    int avail = net.available();
    if (avail <= 0) {
      if (!net.connected() && !net.available()) break;
      delay(5);
      continue;
    }
    int want = (avail > (int)sizeof(buf)) ? (int)sizeof(buf) : avail;
    int n = net.read(buf, want);
    if (n <= 0) continue;
    // mono -> stereo：每样本复制到左右两路（I2S 配的是 RIGHT_LEFT）
    static int16_t st[3200];   // 缓冲 3200 字节=1600 样本，stereo 后需要 1600*2=3200 个元素
    int smp = n / 2;
    if (smp > 1600) smp = 1600;
    for (int i = 0; i < smp; i++) {
      int16_t v = ((int16_t *)buf)[i];
      st[i * 2] = v;
      st[i * 2 + 1] = v;
    }
    size_t written = 0;
    i2s_channel_write(spkHandle, st, smp * 4, &written, portMAX_DELAY);
    got += smp * 2;
    t0 = millis();
  }
  Serial.printf("[喇叭] 放完 %ld 字节\n", got);
  net.stop();
}

// ------------------------------------------------------------------ 录音

void startRecording() {
  if (recording) return;
  recording = true;
  recBytes = 0;
  recStart = millis();
  Serial.println("[mic] 开始录…");
}

void stopRecordingAndSend() {
  if (!recording) return;
  recording = false;
  Serial.printf("[mic] 录了 %.1f 秒，发给她\n", recBytes / 32000.0);
  if (recBytes < 9600) { Serial.println("[mic] 太短，不发了"); return; }
  String heard = postUtter((const uint8_t *)recBuf, recBytes);
  if (heard.length()) Serial.printf("[我] %s\n", heard.c_str());
  pullAndPlay(15000);
}

void pumpMic() {
  static int32_t raw[256];              // 32bit STEREO 原始缓冲（128 帧 L/R）
  if (!recording) {
    size_t dropped;
    i2s_channel_read(micHandle, raw, sizeof(raw), &dropped, 0);
    return;
  }
  size_t n = 0;
  if (i2s_channel_read(micHandle, raw, sizeof(raw), &n, 20 / portTICK_PERIOD_MS) != ESP_OK) return;
  if (!n) return;
  // 32bit STEREO → 16bit 单声道：取偶数索引（左声道），高位右移 16
  int cnt = n / 4;
  int16_t *dst = (int16_t *)((uint8_t *)recBuf + recBytes);
  int out = 0;
  for (int i = 0; i < cnt; i += 2) {
    if (recBytes + (out + 1) * 2 > MAX_REC_BYTES) break;
    dst[out++] = (int16_t)(raw[i] >> 16);
  }
  recBytes += out * 2;
  if (millis() - recStart > MAX_REC_SEC * 1000UL) stopRecordingAndSend();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  recBuf = (int16_t *)ps_malloc(MAX_REC_BYTES);
  if (!recBuf) { Serial.println("[err] PSRAM 没开！Arduino 里把 PSRAM 设成 OPI PSRAM"); while (true) delay(1000); }
  Serial.printf("[ok] 录音缓冲 %u 字节（PSRAM）\n", (unsigned)MAX_REC_BYTES);

  setupI2sMic();
  setupI2sSpk();

  // ---- 开机自检：放 0.6 秒 1kHz 正弦，验证喇叭通路 ----
  {
    Serial.println("[spk] 自检音 1kHz 2秒 60%...");
    static int16_t tone[1024];   // 512 帧 stereo
    unsigned long totalFrames = 0;
    unsigned long wantFrames = (unsigned long)SR * 2;   // 2 秒
    while (totalFrames < wantFrames) {
      for (int i = 0; i < 512; i++) {
        int16_t v = (int16_t)(sinf(2.0f * PI * 1000.0f * (totalFrames + i) / SR) * 20000);
        tone[i * 2] = v;
        tone[i * 2 + 1] = v;
      }
      size_t w = 0;
      esp_err_t e = i2s_channel_write(spkHandle, tone, sizeof(tone), &w, portMAX_DELAY);
      if (totalFrames == 0) Serial.printf("[spk] write ret=%d written=%u/%u\n", (int)e, (unsigned)w, (unsigned)sizeof(tone));
      totalFrames += 512;
    }
    Serial.printf("[spk] 自检音结束，共 %lu 帧\n", totalFrames);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] 连 "); Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(400); }
  Serial.printf("\n[wifi] 拿到 %s\n", WiFi.localIP().toString().c_str());
  connectServer();
  Serial.println("[ready] 按住 BOOT 说话");
}

void loop() {
  // 空闲时喂静音，避免 I2S DMA 反复重播缓冲导致"一直响"
  static uint32_t lastIdle = 0;
  // 16kHz stereo 16bit = 64000 字节/秒 → 每 10ms 需要 640 字节
  if (!recording && millis() - lastIdle >= 10) {
    lastIdle = millis();
    static int16_t silence[320];      // 160 帧 × 2 声道 = 320 个 int16 = 640 字节
    size_t w = 0;
    i2s_channel_write(spkHandle, silence, sizeof(silence), &w, 0);
  }
  static uint32_t lastHb = 0;
  if (millis() - lastHb > 5000) {
    lastHb = millis();
    Serial.printf("[hb] t=%lums heap=%d rec=%d recB=%u\n", millis(), ESP.getFreeHeap(), (int)recording, (unsigned)recBytes);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] 掉了，重连");
    WiFi.reconnect();
    delay(3000);
    return;
  }
  pumpMic();


  // 单击切换：按一下开始录，再按一下结束；最长 10 秒自动结束
  static bool last = false;
  bool pressed = (digitalRead(PIN_BOOT) == LOW);
  if (pressed && !last) {
    delay(30);                       // 消抖
    if (digitalRead(PIN_BOOT) == LOW) {
      if (!recording) {
        Serial.println("[key] 按一下 → 开始录");
        startRecording();
      } else {
        Serial.println("[key] 再按一下 → 结束");
        stopRecordingAndSend();
      }
    }
  }
  last = pressed;
  if (recording && millis() - recStart > 10000) {
    Serial.println("[key] 超 10 秒，自动结束");
    stopRecordingAndSend();
  }

  delay(1);
}