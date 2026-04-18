#pragma once
#include <cstdint>

enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,  // 5-way center press (wakeup only)
};

void initButtons();
ButtonEvent pollButtons(uint32_t nowMs);
