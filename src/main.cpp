#include <Arduino.h>
#include "ui.h"
#include "state.h"

static int stage = 0;
static uint32_t lastSwitch = 0;

static AppState makeIdle() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.total = 3; s.hb.running = 1; s.hb.waiting = 0;
  s.hb.msg = "working on login flow...";
  s.ownerName = "Felix";
  return s;
}

static AppState makePrompt() {
  AppState s;
  s.mode = Mode::Prompt;
  s.hb.hasPrompt = true;
  s.hb.prompt.tool = "Bash";
  s.hb.prompt.hint = "rm -rf /tmp/foo";
  return s;
}

static AppState makeAck(bool approved) {
  AppState s;
  s.mode = Mode::Ack;
  s.ackApproved = approved;
  return s;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  renderBoot("UI smoke test");
  delay(800);
}

void loop() {
  uint32_t now = millis();
  if (now - lastSwitch > 2500) {
    lastSwitch = now;
    Serial.print("stage "); Serial.println(stage);
    switch (stage) {
      case 0: renderAdvertising("Claude-Wio-AB12"); break;
      case 1: renderConnected(); break;
      case 2: renderIdle(makeIdle()); break;
      case 3: renderPrompt(makePrompt()); break;
      case 4: renderAck(makeAck(true)); break;
      case 5: renderAck(makeAck(false)); break;
      case 6: renderDisconnected(); break;
      case 7: renderFatal("BLE init failed"); break;
    }
    stage = (stage + 1) % 8;
  }
}
