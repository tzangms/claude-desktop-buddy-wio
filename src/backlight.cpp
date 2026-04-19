#include "backlight.h"
#include "config.h"
#include "state.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace {
  bool awake = true;
  uint32_t lastActivityMs = 0;

#ifdef ARDUINO
  void writePin(bool high) {
    digitalWrite(LCD_BACKLIGHT, high ? HIGH : LOW);
  }
#else
  int writeCount = 0;
  bool lastWritten = true;
  void writePin(bool high) { lastWritten = high; ++writeCount; }
#endif
}

#ifndef ARDUINO
int _backlightWriteCount() { return writeCount; }
bool _backlightLastWritten() { return lastWritten; }
#endif

void backlightInit() {
#ifdef ARDUINO
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
#else
  writeCount = 0;
  lastWritten = true;
#endif
  awake = true;
  lastActivityMs = 0;
}

void backlightWake(uint32_t /*nowMs*/) {
  // TODO(Task 4): implement wake-on-activity logic.
}

void backlightTick(const AppState& s, uint32_t nowMs) {
  if (!awake) return;
  if (s.mode == Mode::Prompt) {
    lastActivityMs = nowMs;
    return;
  }
  if ((nowMs - lastActivityMs) < BACKLIGHT_IDLE_MS) return;
  writePin(false);
  awake = false;
}

bool backlightIsAwake() {
  return awake;
}
