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
  uint32_t lastHeartbeatMs = 0;
  // ACK display state
  bool ackApproved = false;
  uint32_t ackUntilMs = 0;
};

enum class PermissionDecision { Approve, Deny };

// Pure transition functions. Return true if the state changed in a way
// that requires a re-render.
bool applyHeartbeat(AppState& s, const HeartbeatData& hb, uint32_t nowMs);
bool applyOwner(AppState& s, const std::string& name);
bool applyDisconnect(AppState& s);
bool applyConnected(AppState& s);

// Returns true if a decision should be sent. `out` is set to the decision.
bool applyButton(AppState& s, char button, uint32_t nowMs,
                 PermissionDecision& out, std::string& outPromptId);

// Returns true if mode changed. Call every loop.
bool applyTimeouts(AppState& s, uint32_t nowMs);
