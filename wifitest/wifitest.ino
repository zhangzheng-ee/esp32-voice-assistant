// WiFi 连接测试 —— 连 iPhone 热点
#include <WiFi.h>

const char* SSID = "YOUR_WIFI_SSID";
const char* PASS = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== WiFi connect test ===");
  Serial.printf("SSID: %s\n", SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(SSID, PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("=== CONNECTED ===");
    Serial.printf("IP:   %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("GW:   %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("CH:   %d\n", WiFi.channel());
    Serial.printf("MAC:  %s\n", WiFi.macAddress().c_str());
    Serial.println("=== OK ===");
  } else {
    Serial.println("=== FAILED ===");
    Serial.printf("status code: %d\n", (int)WiFi.status());
    Serial.println("1=NO_SSID 4=CONNECT_FAILED 6=DISCONNECTED");
    Serial.println("常见原因: 热点是5GHz / 密码错 / iPhone没开最大兼容性");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("alive rssi=%d\n", WiFi.RSSI());
  } else {
    Serial.println("lost");
  }
  delay(5000);
}