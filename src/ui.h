#pragma once

#include "state.h"

void initUi();
void renderBoot(const char* msg);
void renderAdvertising(const char* deviceName);
void renderConnected();
void renderIdle(const AppState& s);
void renderPrompt(const AppState& s);
void renderAck(const AppState& s);
void renderDisconnected();
void renderFatal(const char* msg);
void setBacklight(uint8_t pct);  // 0-100
