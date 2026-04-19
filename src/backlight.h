#pragma once

#include <cstdint>

struct AppState;

void backlightInit();
void backlightWake(uint32_t nowMs);
void backlightTick(const AppState& s, uint32_t nowMs);
bool backlightIsAwake();

#ifndef ARDUINO
int _backlightWriteCount();
bool _backlightLastWritten();
#endif
