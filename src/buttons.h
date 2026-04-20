#pragma once
#include <cstdint>

enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,       // 5-way center: instant press edge
  LongPressNav,   // 5-way center: emitted once after hold exceeds BUTTON_LONG_PRESS_MS
  PressLeft,      // 5-way left edge
  PressRight,     // 5-way right edge
};

void initButtons();
ButtonEvent pollButtons(uint32_t nowMs);
