void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("========== ESP32-S3 开机自检 ==========");
  Serial.printf("芯片型号 : %s\n", ESP.getChipModel());
  Serial.printf("核心数   : %d\n", ESP.getChipCores());
  Serial.printf("主频     : %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash    : %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("PSRAM    : %d MB\n", ESP.getPsramSize() / (1024 * 1024));
  Serial.printf("可用堆   : %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.println("=======================================");
}

void loop() {
  Serial.printf("[%lu 秒] 运行中，可用堆 %d KB\n",
                millis() / 1000, ESP.getFreeHeap() / 1024);
  delay(2000);
}