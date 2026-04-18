#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "protocol.h"
#include "ui.h"
#include "buttons.h"
#include "ble_nus.h"

static AppState appState;
static Mode lastRenderedMode = Mode::BleInit;
static std::string lastRenderedPromptId;
static uint32_t lastButtonSendMs = 0;
static uint32_t lastInteractionMs = 0;
static volatile bool pendingRender = false;
static std::string cachedSuffix;

static std::string deviceSuffix() {
  if (!cachedSuffix.empty()) return cachedSuffix;
  // SAMD51 serial word 3 (0x008061FC); last 16 bits → 4-hex stable suffix.
  uint32_t id = *(volatile uint32_t*)0x008061FC;
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", (unsigned)(id & 0xFFFF));
  cachedSuffix = buf;
  return cachedSuffix;
}

static void markInteraction(uint32_t now) {
  lastInteractionMs = now;
  setBacklight(100);
}

static void render(bool force) {
  bool modeChanged = appState.mode != lastRenderedMode;
  bool promptChanged = appState.hb.prompt.id != lastRenderedPromptId;
  if (!force && !modeChanged && !promptChanged) return;

  switch (appState.mode) {
    case Mode::BleInit:      renderBoot("BLE init..."); break;
    case Mode::Advertising:  {
      std::string n = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
      renderAdvertising(n.c_str()); break;
    }
    case Mode::Connected:    renderConnected(); break;
    case Mode::Idle:         renderIdle(appState); break;
    case Mode::Prompt:       renderPrompt(appState); break;
    case Mode::Ack:          renderAck(appState); break;
    case Mode::Disconnected: renderDisconnected(); break;
    case Mode::Fatal:        renderFatal("see serial"); break;
  }
  lastRenderedMode = appState.mode;
  lastRenderedPromptId = appState.hb.prompt.id;
}

static void onLine(const std::string& line) {
  // Runs on the rpcBLE callback path — keep it cheap. Defer SPI to loop().
  uint32_t now = millis();
  ParsedMessage m = parseLine(line);
  switch (m.kind) {
    case MessageKind::Heartbeat:
      if (appState.mode == Mode::Connected ||
          appState.mode == Mode::Disconnected) {
        applyConnected(appState);
      }
      applyHeartbeat(appState, m.heartbeat, now);
      pendingRender = true;
      break;
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      break;
    case MessageKind::Time:
    case MessageKind::Unknown:
    case MessageKind::ParseError:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  initButtons();
  renderBoot("BLE init...");

  appState.mode = Mode::BleInit;
  if (!initBle(deviceSuffix(), onLine)) {
    appState.mode = Mode::Fatal;
    render(true);
    while (1) delay(1000);
  }
  appState.mode = Mode::Advertising;
  render(true);
}

void loop() {
  uint32_t now = millis();
  pollBle();

  if (pendingRender) {
    pendingRender = false;
    render(true);
  }

  bool conn = isBleConnected();
  if (conn && appState.mode == Mode::Advertising) {
    applyConnected(appState);
    render(true);
  } else if (!conn && (appState.mode == Mode::Idle ||
                      appState.mode == Mode::Prompt ||
                      appState.mode == Mode::Ack ||
                      appState.mode == Mode::Connected)) {
    applyDisconnect(appState);
    render(true);
  } else if (!conn && appState.mode == Mode::Disconnected &&
             lastRenderedMode != Mode::Advertising) {
    appState.mode = Mode::Advertising;
    render(true);
  }

  if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS) {
    ButtonEvent e = pollButtons(now);
    if (e == ButtonEvent::PressNav) {
      markInteraction(now);
    } else if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
      markInteraction(now);
      char btn = (e == ButtonEvent::PressA) ? 'A' : 'C';
      PermissionDecision d;
      std::string id;
      if (applyButton(appState, btn, now, d, id)) {
        std::string line = formatPermission(id, d);
        sendLine(line);
        lastButtonSendMs = now;
        render(true);
      }
    }
  }

  if (applyTimeouts(appState, now)) render(true);

  if (appState.mode == Mode::Idle &&
      lastInteractionMs != 0 &&
      (now - lastInteractionMs) > BACKLIGHT_IDLE_MS) {
    setBacklight(20);
  }

  delay(10);
}
