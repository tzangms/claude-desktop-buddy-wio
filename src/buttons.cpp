#include "buttons.h"
#include "config.h"
#include <Arduino.h>

struct Btn {
  uint8_t pin;
  bool lastRaw;
  bool stable;
  uint32_t lastChangeMs;
  ButtonEvent evt;
};

static Btn btns[] = {
  {WIO_KEY_A,    true, true, 0, ButtonEvent::PressA},
  {WIO_KEY_B,    true, true, 0, ButtonEvent::PressB},
  {WIO_KEY_C,    true, true, 0, ButtonEvent::PressC},
  {WIO_5S_PRESS, true, true, 0, ButtonEvent::PressNav},
};

void initButtons() {
  for (auto& b : btns) {
    pinMode(b.pin, INPUT_PULLUP);
    b.lastRaw = digitalRead(b.pin);
    b.stable = b.lastRaw;
  }
}

ButtonEvent pollButtons(uint32_t nowMs) {
  for (auto& b : btns) {
    bool raw = digitalRead(b.pin);
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChangeMs = nowMs;
    }
    if ((nowMs - b.lastChangeMs) >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      // Active-low: press = LOW
      if (raw == LOW) return b.evt;
    }
  }
  return ButtonEvent::None;
}
