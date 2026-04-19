#include "state.h"
#include "config.h"

bool applyHeartbeat(AppState& s, HeartbeatData hb, uint32_t nowMs) {
  Mode prev = s.mode;
  std::string prevPromptId = s.hb.prompt.id;
  bool prevHasPrompt = s.hb.hasPrompt;
  bool hasPrompt = hb.hasPrompt;
  std::string newPromptId = hb.prompt.id;
  s.hb = std::move(hb);
  s.lastHeartbeatMs = nowMs;

  if (hasPrompt) {
    s.mode = Mode::Prompt;
    if (!prevHasPrompt || prevPromptId != newPromptId) {
      s.promptArrivedMs = nowMs;
    }
  } else if (s.mode != Mode::Ack) {
    s.mode = Mode::Idle;
  }
  return s.mode != prev || s.hb.prompt.id != prevPromptId;
}

bool applyOwner(AppState& s, const std::string& name) {
  if (s.ownerName == name) return false;
  s.ownerName = name;
  return true;
}

bool applyDisconnect(AppState& s) {
  if (s.mode == Mode::Disconnected) return false;
  s.mode = Mode::Disconnected;
  return true;
}

bool applyConnected(AppState& s) {
  if (s.mode == Mode::Connected) return false;
  s.mode = Mode::Connected;
  return true;
}

bool applyButton(AppState& s, char button, uint32_t nowMs,
                 PermissionDecision& out, std::string& outPromptId) {
  if (s.mode != Mode::Prompt) return false;
  if (button != 'A' && button != 'C') return false;
  out = (button == 'A') ? PermissionDecision::Approve
                        : PermissionDecision::Deny;
  outPromptId = s.hb.prompt.id;
  s.ackApproved = (button == 'A');
  s.ackUntilMs = nowMs + ACK_DISPLAY_MS;
  s.mode = Mode::Ack;
  // Once a decision is sent, the current prompt is resolved locally.
  // Clear so ACK expiration falls through to Idle until the next heartbeat.
  s.hb.hasPrompt = false;
  s.hb.prompt = PromptData{};
  return true;
}

bool applyTimeouts(AppState& s, uint32_t nowMs) {
  if (s.mode == Mode::Ack && nowMs >= s.ackUntilMs) {
    s.mode = s.hb.hasPrompt ? Mode::Prompt : Mode::Idle;
    return true;
  }
  bool live = (s.mode == Mode::Idle || s.mode == Mode::Prompt ||
               s.mode == Mode::Ack);
  if (live && (nowMs - s.lastHeartbeatMs) > HEARTBEAT_TIMEOUT_MS) {
    s.mode = Mode::Disconnected;
    return true;
  }
  return false;
}

bool applyNameCmd(AppState& s, const std::string& name, std::string& err) {
  if (name.empty()) {
    err = "empty name";
    return false;
  }
  std::string n = name;
  if (n.size() > NAME_CHARS_MAX) n.resize(NAME_CHARS_MAX);
  s.deviceName = std::move(n);
  return true;
}

void applyTime(AppState& s, int64_t epoch, int32_t offsetSec, uint32_t nowMs) {
  s.timeEpoch = epoch;
  s.timeOffsetSec = offsetSec;
  s.timeSetAtMs = nowMs;
}
