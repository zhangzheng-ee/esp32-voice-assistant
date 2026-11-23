// 阶段5 step1: WiFi + HTTPS 调 DeepSeek
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
const char* API_KEY = DEEPSEEK_API_KEY;
const char* API_HOST = "api.deepseek.com";
const int   API_PORT = 443;
const char* MODEL    = "deepseek-chat";

// 从 JSON 里抠出 "content":"..." 的内容，处理常见转义
bool extractContent(const String& src, String& out) {
  int p = src.indexOf("\"content\":");
  if (p < 0) return false;
  p += 10;
  // 跳过 "content": 后面的空白（是否有空格因服务端实现而异）
  while (p < (int)src.length() && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r' || src[p] == '\n')) p++;
  if (p >= (int)src.length() || src[p] != '"') return false;
  p++;
  String r;
  for (int i = p; i < (int)src.length(); i++) {
    char c = src[i];
    if (c == '\\') {
      if (i + 1 >= (int)src.length()) break;
      char n = src[++i];
      if (n == 'n') r += '\n';
      else if (n == 't') r += '\t';
      else if (n == '"') r += '"';
      else if (n == '\\') r += '\\';
      else r += n;
    } else if (c == '"') {
      out = r;
      return true;
    } else {
      r += c;
    }
  }
  return false;
}

bool askAI(const char* question, String& answer) {
  WiFiClientSecure client;
  client.setInsecure();           // 跳过证书校验，本地测试够用
  client.setTimeout(30);

  Serial.printf("[http] connecting %s:%d ...\n", API_HOST, API_PORT);
  unsigned long t0 = millis();
  if (!client.connect(API_HOST, API_PORT)) {
    Serial.println("[http] connect FAILED");
    return false;
  }
  Serial.printf("[http] tcp ok, %lu ms\n", millis() - t0);

  // 构造 body（question 里的引号要转义）
  String q = String(question);
  q.replace("\\", "\\\\");
  q.replace("\"", "\\\"");
  String body = String("{\"model\":\"") + MODEL +
                "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + q +
                "\"}],\"max_tokens\":400}";

  client.print("POST /v1/chat/completions HTTP/1.1\r\n");
  client.print("Host: "); client.print(API_HOST); client.print("\r\n");
  client.print("Authorization: Bearer "); client.print(API_KEY); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(body);

  // 读响应
  String raw;
  unsigned long last = millis();
  while (client.connected() || client.available()) {
    if (client.available()) {
      raw += client.readStringUntil('\n');
      raw += '\n';
      last = millis();
    } else if (millis() - last > 8000) {
      break;
    }
    if (raw.length() > 8000) break;
  }
  client.stop();
  Serial.printf("[http] resp %d bytes, total %lu ms\n", raw.length(), millis() - t0);
  Serial.println("[raw] >>>");
  Serial.println(raw.substring(0, min((int)raw.length(), 1200)));
  Serial.println("[raw] <<<");

  if (raw.indexOf("HTTP/1.1 200") < 0) {
    int e = raw.indexOf("\r\n\r\n");
    Serial.println("[err] not 200:");
    Serial.println(e > 0 ? raw.substring(e + 4, min((int)raw.length(), e + 300)) : raw.substring(0, 300));
    return false;
  }
  if (!extractContent(raw, answer)) {
    Serial.println("[parse] FAILED, raw dump:");
    Serial.println(raw.substring(0, min((int)raw.length(), 1000)));
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== stage5: WiFi + DeepSeek ===");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SSID, PASS);
  Serial.print("[wifi] connecting");
  int n = 0;
  while (WiFi.status() != WL_CONNECTED && n < 40) { delay(500); Serial.print("."); n++; }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] FAILED");
    return;
  }
  Serial.printf("[wifi] OK ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  Serial.println("[oom] free heap = " + String(ESP.getFreeHeap()));

  String ans;
  unsigned long t = millis();
  if (askAI("用20个字以内介绍你自己", ans)) {
    Serial.println("----- ANSWER -----");
    Serial.println(ans);
    Serial.println("------------------");
    Serial.printf("[done] %lu ms\n", millis() - t);
  } else {
    Serial.println("[fail] 请求失败，看上面日志");
  }
  Serial.printf("[heap] %d\n", ESP.getFreeHeap());
}

void loop() { delay(10000); }