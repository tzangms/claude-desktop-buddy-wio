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
#include "pet.h"
#include "xfer.h"

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
    case Mode::FactoryResetConfirm: renderFactoryResetConfirm(modeChanged); break;
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
      int32_t lvlBefore = persistGet().lvl;
      applyHeartbeat(appState, std::move(m.heartbeat), now);
      persistUpdateFromHeartbeat(appState.hb.tokens, appState.hb.tokens_today);
      persistCommit(false);
      if (persistGet().lvl > lvlBefore) petTriggerCelebrate(now);
      if (newPrompt) backlightWake(now);
      pendingRender = true;
      break;
    }
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      // Ack before the QSPI flash write so the ~30-50ms flush doesn't
      // miss the desktop's connect-time timeout window.
      sendLine(formatAck("owner", true));
      persistSetOwnerName(m.ownerName.c_str());
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
      sendLine(formatAck("name", ok, err));
      if (ok) persistSetDeviceName(appState.deviceName.c_str());
      break;
    }
    case MessageKind::UnpairCmd:
      sendLine(formatAck("unpair", true));
      break;
    // The five xfer commands all do SFUD I/O which wedges rpcBLE if run
    // on the callback stack (see persist.cpp:114 comment + MEMORY.md). We
    // only stash the cmd here; loop() calls xferTick() which does the work
    // and emits the ack.
    case MessageKind::CharBegin:
      if (!xferQueueCharBegin(m.xferName.c_str(), m.xferTotal)) {
        sendLine(formatAck("char_begin", false, "busy"));
      }
      break;
    case MessageKind::FileBegin:
      if (!xferQueueFileBegin(m.xferPath.c_str(), m.xferSize)) {
        sendLine(formatAck("file", false, "busy"));
      }
      break;
    case MessageKind::Chunk:
      if (!xferQueueChunk(m.xferChunk.c_str())) {
        sendLine(formatAckN("chunk", false, 0, "busy"));
      }
      break;
    case MessageKind::FileEnd:
      if (!xferQueueFileEnd()) {
        sendLine(formatAckN("file_end", false, 0, "busy"));
      }
      break;
    case MessageKind::CharEnd:
      if (!xferQueueCharEnd()) {
        sendLine(formatAck("char_end", false, "busy"));
      }
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
  xferInit();
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

  // Drain xfer cmd queued by onLine (SFUD runs here, not on BLE callback).
  XferAckInfo a = xferTick();
  switch (a.kind) {
    case XferAckInfo::CharBegin: sendLine(formatAck("char_begin", a.ok)); break;
    case XferAckInfo::FileBegin: sendLine(formatAck("file", a.ok)); break;
    case XferAckInfo::Chunk:     sendLine(formatAckN("chunk", a.ok, a.n)); break;
    case XferAckInfo::FileEnd:   sendLine(formatAckN("file_end", a.ok, a.n)); break;
    case XferAckInfo::CharEnd:   sendLine(formatAck("char_end", a.ok)); break;
    case XferAckInfo::None: break;
  }

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
    if (e == ButtonEvent::LongPressNav) {
      if (applyEnterFactoryResetConfirm(appState)) render(true);
    } else if (appState.mode == Mode::FactoryResetConfirm) {
      if (e == ButtonEvent::PressA) {
        persistFactoryReset();
        delay(100);  // flush serial
        NVIC_SystemReset();
        // unreachable
      } else if (e == ButtonEvent::PressC) {
        applyCancelFactoryReset(appState);
        render(true);
      }
    } else if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
      char btn = (e == ButtonEvent::PressA) ? 'A' : 'C';
      uint32_t promptAge = now - appState.promptArrivedMs;
      PermissionDecision d;
      std::string id;
      if (applyButton(appState, btn, now, d, id)) {
        // Send the permission decision first so the desktop sees it with
        // minimum latency; the counter flush can take 30-50ms.
        std::string line = formatPermission(id, d);
        sendLine(line);
        if (btn == 'A') {
          if (promptAge < PET_APPROVE_HEART_WINDOW_MS) petTriggerHeart(now);
          persistIncAppr();
        } else {
          persistIncDeny();
        }
        lastButtonSendMs = now;
        render(true);
      }
    }
  }

  if (applyTimeouts(appState, now)) render(true);

  backlightTick(appState, now);
  persistTick(now);
  // Pet is only visible on the Idle screen; only gating renders there
  // prevents the 500ms frame tick from repainting Advertising / Connected
  // / Disconnected screens (which previously caused visible flicker).
  bool petAdvanced = petTickFrame(now);
  if (petAdvanced && appState.mode == Mode::Idle) pendingRender = true;

  delay(10);
}
