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

// Long-press tracking for the 5-way center (last entry in btns[]).
static uint32_t navPressedSinceMs = 0;
static bool navLongReported = false;

void initButtons() {
  for (auto& b : btns) {
    pinMode(b.pin, INPUT_PULLUP);
    b.lastRaw = digitalRead(b.pin);
    b.stable = b.lastRaw;
  }
  navPressedSinceMs = 0;
  navLongReported = false;
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
      if (raw == LOW) {
        if (b.evt == ButtonEvent::PressNav) {
          navPressedSinceMs = nowMs;
          navLongReported = false;
        }
        return b.evt;
      } else {
        // Released
        if (b.evt == ButtonEvent::PressNav) {
          navPressedSinceMs = 0;
          navLongReported = false;
        }
      }
    }
  }
  // Long-press detection for 5-way: emitted once while still held.
  if (!navLongReported && navPressedSinceMs != 0 &&
      (nowMs - navPressedSinceMs) >= BUTTON_LONG_PRESS_MS) {
    navLongReported = true;
    return ButtonEvent::LongPressNav;
  }
  return ButtonEvent::None;
}
