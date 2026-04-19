#pragma once
#include <cstdint>

enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,       // 5-way center: instant press edge
  LongPressNav,   // 5-way center: emitted once after hold exceeds BUTTON_LONG_PRESS_MS
};

void initButtons();
ButtonEvent pollButtons(uint32_t nowMs);
