// 阶段5 完整语音助手: 录音 -> ASR -> 大模型 -> TTS -> 播放
// 麦 SCK=4 WS=5 SD=6 | 功放 BCLK=15 LRC=16 DIN=17 SD->3V3 | 屏 12/11/9/10/14

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "driver/i2s_std.h"
#include <Arduino_GFX_Library.h>

#define MIC_SCK 4
#define MIC_WS  5
#define MIC_SD  6

#define SPK_BCLK 15
#define SPK_LRC  16
#define SPK_DIN  17

#define MIC_SR   8000
#define REC_SEC  3
#define REC_N    (MIC_SR * REC_SEC)
#define GAIN     30
#define HP_ALPHA 0.96f

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC   9
#define TFT_CS   10
#define TFT_RST  14

const char* SRV_HOST = "192.168.1.100";
const int   ASR_PORT = 8080;
const int   TTS_PORT = 8081;

#include "secrets.h"
const char* API_KEY = DEEPSEEK_API_KEY;
const char* API_HOST = "api.deepseek.com";
const char* MODEL    = "deepseek-chat";

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_GC9A01(bus, TFT_RST, 0, true);

i2s_chan_handle_t rx = NULL;
i2s_chan_handle_t tx = NULL;
int16_t* pcm = nullptr;

void scr(uint16_t c) { if (gfx) gfx->fillScreen(c); }

// ================= 录音 =================
bool recordPcm() {
  scr(0xFFFF);
  size_t got = 0;
  static int32_t raw[512];
  float xPrev = 0, yPrev = 0;
  int n = 0;
  while (n < REC_N) {
    if (i2s_channel_read(rx, raw, sizeof(raw), &got, 1000) != ESP_OK) break;
    int cnt = got / 4;
    for (int i = 0; i + 1 < cnt; i += 2) {
      if (n >= REC_N) break;
      float x = (float)(raw[i] >> 16) * GAIN;
      float y = HP_ALPHA * (yPrev + x - xPrev);
      xPrev = x; yPrev = y;
      if (y > 32767) y = 32767;
      if (y < -32768) y = -32768;
      pcm[n++] = (int16_t)y;
    }
  }
  scr(0x0000);
  return n > MIC_SR / 4;
}

// ================= ASR =================
bool postAsr(String& out) {
  WiFiClient client;
  client.setTimeout(20);
  if (!client.connect(SRV_HOST, ASR_PORT)) return false;
  int bytes = REC_N * 2;
  client.printf("POST /asr HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/octet-stream\r\nX-Sample-Rate: %d\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                SRV_HOST, ASR_PORT, MIC_SR, bytes);
  client.write((uint8_t*)pcm, bytes);
  String raw; unsigned long last = millis();
  while (client.connected() || client.available()) {
    if (client.available()) { raw += client.readStringUntil('\n'); raw += '\n'; last = millis(); }
    else if (millis() - last > 15000) break;
    if (raw.length() > 6000) break;
  }
  client.stop();
  if (raw.indexOf("HTTP/1.1 200") < 0) return false;
  int p = raw.indexOf("\"text\":");
  if (p < 0) return false;
  p += 7;
  while (p < (int)raw.length() && (raw[p] == ' ' || raw[p] == '"')) p++;
  int e = raw.indexOf('"', p);
  if (e < 0) e = raw.length();
  out = raw.substring(p, e);
  out.trim();
  return true;
}

// ================= 大模型 =================
bool askAI(const String& question, String& answer) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);
  if (!client.connect(API_HOST, 443)) return false;

  String q = question;
  q.replace("\\", "\\\\");
  q.replace("\"", "\\\"");
  String body = String("{\"model\":\"") + MODEL +
                "\",\"messages\":[{\"role\":\"system\",\"content\":\"你是桌面上的小AI，回答简短口语化，30字以内，不要用括号或表情。\"},{\"role\":\"user\",\"content\":\"" + q +
                "\"}],\"max_tokens\":300}";

  client.print("POST /v1/chat/completions HTTP/1.1\r\n");
  client.print("Host: "); client.print(API_HOST); client.print("\r\n");
  client.print("Authorization: Bearer "); client.print(API_KEY); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(body);

  String raw; unsigned long last = millis();
  while (client.connected() || client.available()) {
    if (client.available()) { raw += client.readStringUntil('\n'); raw += '\n'; last = millis(); }
    else if (millis() - last > 10000) break;
    if (raw.length() > 9000) break;
  }
  client.stop();

  int idx = raw.indexOf("\"content\":");
  if (idx < 0) return false;
  idx += 10;
  while (idx < (int)raw.length() && (raw[idx] == ' ' || raw[idx] == '\t' || raw[idx] == '\r' || raw[idx] == '\n')) idx++;
  if (idx >= (int)raw.length() || raw[idx] != '"') return false;
  idx++;
  String r;
  for (int i = idx; i < (int)raw.length(); i++) {
    char c = raw[i];
    if (c == '\\') {
      if (i + 1 >= (int)raw.length()) break;
      char nx = raw[++i];
      if (nx == 'n') r += '\n';
      else if (nx == 't') r += '\t';
      else if (nx == '"') r += '"';
      else if (nx == '\\') r += '\\';
      else r += nx;
    } else if (c == '"') { answer = r; return true; }
    else r += c;
  }
  return false;
}

// ================= TTS 下载并播放 =================
// 把文字 POST 给电脑 TTS，收到 wav 直接流式喂 I2S（不整段缓存）
bool speakText(const String& text) {
  WiFiClient client;
  client.setTimeout(30);
  if (!client.connect(SRV_HOST, TTS_PORT)) { Serial.println("[tts] connect FAIL"); return false; }

  String body = text;
  client.printf("POST /tts HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                SRV_HOST, TTS_PORT, body.length());
  client.print(body);

  // 跳过 HTTP 响应头：逐字节找 \r\n\r\n（不用 readStringUntil，避免吞掉二进制）
  int st = 0;
  unsigned long th = millis();
  while (millis() - th < 8000) {
    if (!client.available()) {
      if (!client.connected()) break;
      delay(2);
      continue;
    }
    int c = client.read();
    if (c < 0) break;
    if (c == '\r') { st = (st == 0 || st == 2) ? st + 1 : 1; }
    else if (c == '\n') {
      if (st == 1) st = 2;
      else if (st == 3) break;   // \r\n\r\n 结束
      else st = 0;
    } else st = 0;
  }

  // 读 wav 头（44 字节标准，"data" 在偏移 36）
  uint8_t hdr[64];
  int got = 0;
  unsigned long t0 = millis();
  while (got < (int)sizeof(hdr) && millis() - t0 < 5000) {
    int a = client.available();
    if (a > 0) { got += client.read(hdr + got, min(a, (int)sizeof(hdr) - got)); }
    else if (!client.connected()) break;
    else delay(3);
  }
  int dataOff = -1;
  for (int i = 0; i + 4 <= got; i++) {
    if (hdr[i] == 'd' && hdr[i+1] == 'a' && hdr[i+2] == 't' && hdr[i+3] == 'a') { dataOff = i + 8; break; }
  }
  if (dataOff < 0) { Serial.println("[tts] no data chunk"); client.stop(); return false; }

  // 先播头里已经带的 PCM
  int firstPcm = got - dataOff;
  scr(0x07E0);   // 绿 = 正在说
  if (firstPcm > 0) {
    size_t w;
    i2s_channel_write(tx, hdr + dataOff, firstPcm, &w, 1000);
  }
  // 再流式播剩余
  static uint8_t buf[1024];
  uint32_t total = firstPcm;
  unsigned long idle = millis();
  while (millis() - idle < 6000) {
    int a = client.available();
    if (a > 0) {
      int n = client.read(buf, min(a, (int)sizeof(buf)));
      if (n > 0) {
        size_t w;
        i2s_channel_write(tx, buf, n, &w, 2000);
        total += n;
        idle = millis();
      }
    } else if (!client.connected()) {
      break;
    } else {
      delay(3);
    }
  }
  client.stop();
  scr(0x0000);
  Serial.printf("[tts] played %u bytes\n", total);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== 语音助手 v1 ===");

  if (!gfx->begin()) Serial.println("[tft] FAIL");
  scr(0x0000);

  pcm = (int16_t*)ps_malloc(REC_N * 2);
  if (!pcm) { Serial.println("[mem] FAIL"); while (1) delay(1000); }

  // --- I2S RX 麦克风 ---
  i2s_chan_config_t rcfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  rcfg.dma_desc_num = 8; rcfg.dma_frame_num = 256;
  ESP_ERROR_CHECK(i2s_new_channel(&rcfg, NULL, &rx));
  i2s_std_config_t rstd = {
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
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &rstd));
  ESP_ERROR_CHECK(i2s_channel_enable(rx));

  // --- I2S TX 功放（22050Hz，与 TTS 输出一致） ---
  i2s_chan_config_t tcfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  tcfg.dma_desc_num = 8; tcfg.dma_frame_num = 256;
  ESP_ERROR_CHECK(i2s_new_channel(&tcfg, &tx, NULL));
  i2s_std_config_t tstd = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(22050),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)SPK_BCLK,
      .ws   = (gpio_num_t)SPK_LRC,
      .dout = (gpio_num_t)SPK_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &tstd));
  ESP_ERROR_CHECK(i2s_channel_enable(tx));

  delay(400);
  { size_t g; int32_t tmp[256]; i2s_channel_read(rx, tmp, sizeof(tmp), &g, 500); }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SSID, PASS);
  Serial.print("[wifi] connecting");
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) { delay(500); Serial.print("."); t++; }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[wifi] FAILED"); return; }
  Serial.printf("[wifi] OK ip=%s rssi=%d heap=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI(), ESP.getFreeHeap());
  Serial.println("说话吧，屏幕白=在听，绿=在说");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { delay(2000); return; }

  if (!recordPcm()) { delay(500); return; }

  String text;
  if (!postAsr(text) || text.length() == 0) { Serial.println("[asr] 没听清"); delay(600); return; }
  Serial.println("----- 你 -----"); Serial.println(text);

  String ans;
  unsigned long t0 = millis();
  if (!askAI(text, ans)) { Serial.println("[ai] 失败"); delay(600); return; }
  Serial.println("----- 它 -----"); Serial.println(ans);

  speakText(ans);
  Serial.printf("----- 全程 %lu ms, heap %d -----\n", millis() - t0, ESP.getFreeHeap());
  delay(600);
}