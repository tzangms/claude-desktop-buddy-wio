#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "protocol.h"
#include "status.h"
#include "ui.h"
#include "buttons.h"
#include "ble_nus.h"
#include "mem.h"
#include "backlight.h"
#include "persist.h"

static AppState appState;
static Mode lastRenderedMode = Mode::BleInit;
static std::string lastRenderedPromptId;
static uint32_t lastButtonSendMs = 0;
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
    case Mode::Idle:         renderIdle(appState, modeChanged); break;
    case Mode::Prompt:       renderPrompt(appState, modeChanged || promptChanged); break;
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
    case MessageKind::Heartbeat: {
      if (appState.mode == Mode::Connected ||
          appState.mode == Mode::Disconnected) {
        applyConnected(appState);
      }
      bool newPrompt = m.heartbeat.hasPrompt &&
                       (!appState.hb.hasPrompt ||
                        m.heartbeat.prompt.id != appState.hb.prompt.id);
      applyHeartbeat(appState, std::move(m.heartbeat), now);
      persistUpdateFromHeartbeat(appState.hb.tokens, appState.hb.tokens_today);
      persistCommit(false);
      if (newPrompt) backlightWake(now);
      pendingRender = true;
      break;
    }
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      persistSetOwnerName(m.ownerName.c_str());
      sendLine(formatAck("owner", true));
      break;
    case MessageKind::Time:
      applyTime(appState, m.timeEpoch, m.timeOffsetSec, now);
      break;
    case MessageKind::StatusCmd: {
      StatusSnapshot snap = captureStatus(appState, now);
      sendLine(formatStatusAck(snap));
      break;
    }
    case MessageKind::NameCmd: {
      std::string err;
      bool ok = applyNameCmd(appState, m.nameValue, err);
      if (ok) persistSetDeviceName(appState.deviceName.c_str());
      sendLine(formatAck("name", ok, err));
      break;
    }
    case MessageKind::UnpairCmd:
      sendLine(formatAck("unpair", true));
      break;
    case MessageKind::TurnEvent:
      break;
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
  backlightInit();
  persistInit();
  if (persistGet().deviceName[0] == '\0') {
    std::string def = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
    persistSetDeviceName(def.c_str());
  }
  renderBoot("BLE init...");

  appState.deviceName = persistGet().deviceName;
  appState.ownerName  = persistGet().ownerName;
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
    if (e != ButtonEvent::None) {
      backlightWake(now);
    }
    if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
      char btn = (e == ButtonEvent::PressA) ? 'A' : 'C';
      PermissionDecision d;
      std::string id;
      if (applyButton(appState, btn, now, d, id)) {
        if (btn == 'A') persistIncAppr();
        else            persistIncDeny();
        std::string line = formatPermission(id, d);
        sendLine(line);
        lastButtonSendMs = now;
        render(true);
      }
    }
  }

  if (applyTimeouts(appState, now)) render(true);

  backlightTick(appState, now);
  persistTick(now);

  delay(10);
}
