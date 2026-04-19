#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Mode {
  BleInit,
  Advertising,
  Connected,     // connected but no heartbeat yet
  Idle,
  Prompt,
  Ack,
  Disconnected,
  Fatal,
};

struct PromptData {
  std::string id;
  std::string tool;
  std::string hint;
};

struct HeartbeatData {
  int total = 0;
  int running = 0;
  int waiting = 0;
  std::string msg;
  std::vector<std::string> entries;
  int64_t tokens = 0;
  int64_t tokens_today = 0;
  bool hasPrompt = false;
  PromptData prompt;
};

struct AppState {
  Mode mode = Mode::BleInit;
  HeartbeatData hb;
  std::string ownerName;
  std::string deviceName;
  int64_t     timeEpoch = 0;
  int32_t     timeOffsetSec = 0;
  uint32_t    timeSetAtMs = 0;
  uint32_t lastHeartbeatMs = 0;
  uint32_t promptArrivedMs = 0;  // millis() stamp for the current prompt
  bool ackApproved = false;
  uint32_t ackUntilMs = 0;
};

enum class PermissionDecision { Approve, Deny };

// Pure transition functions. Return true if the state changed in a way
// that requires a re-render.
bool applyHeartbeat(AppState& s, HeartbeatData hb, uint32_t nowMs);
bool applyOwner(AppState& s, const std::string& name);
bool applyDisconnect(AppState& s);
bool applyConnected(AppState& s);

// Returns true if a decision should be sent. `out` is set to the decision.
bool applyButton(AppState& s, char button, uint32_t nowMs,
                 PermissionDecision& out, std::string& outPromptId);

// Returns true if mode changed. Call every loop.
bool applyTimeouts(AppState& s, uint32_t nowMs);

// Update deviceName in-memory. Rejects empty; truncates to NAME_CHARS_MAX.
// Returns true if accepted, false if rejected. `err` is set on rejection.
bool applyNameCmd(AppState& s, const std::string& name, std::string& err);

// Store time sync (epoch + tz offset + local millis stamp).
void applyTime(AppState& s, int64_t epoch, int32_t offsetSec, uint32_t nowMs);
