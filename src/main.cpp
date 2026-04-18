#include <Arduino.h>
#include "ui.h"
#include "ble_nus.h"

static void onLine(const std::string& line) {
  Serial.print("RX: "); Serial.println(line.c_str());
  sendLine(line + "\n");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  renderBoot("BLE init...");
  if (!initBle("TEST", onLine)) {
    renderFatal("BLE init failed");
    while (1) delay(1000);
  }
  renderAdvertising("Claude-Wio-TEST");
}

void loop() {
  pollBle();
  static bool wasConn = false;
  bool nowConn = isBleConnected();
  if (nowConn && !wasConn) renderConnected();
  if (!nowConn && wasConn)  renderAdvertising("Claude-Wio-TEST");
  wasConn = nowConn;
  delay(20);
}
