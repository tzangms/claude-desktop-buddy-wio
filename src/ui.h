#pragma once

#include "state.h"

void initUi();
void renderBoot(const char* msg);
void renderAdvertising(const char* deviceName);
void renderConnected();
void renderIdle(const AppState& s, bool fullRedraw);
void renderPrompt(const AppState& s, bool fullRedraw);
void renderAck(const AppState& s);
void renderDisconnected();
void renderFatal(const char* msg);
