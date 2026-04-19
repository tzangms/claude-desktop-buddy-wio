#pragma once

#include <string>
#include "state.h"

enum class MessageKind {
  Heartbeat,
  Owner,
  Time,
  TurnEvent,
  StatusCmd,
  NameCmd,
  UnpairCmd,
  Unknown,
  ParseError,
};

struct ParsedMessage {
  MessageKind kind = MessageKind::Unknown;
  HeartbeatData heartbeat;
  std::string ownerName;
  int32_t timeEpoch = 0;
  int32_t timeOffsetSec = 0;
};

// Parse one line of JSON (already stripped of trailing '\n').
ParsedMessage parseLine(const std::string& line);

// Build a permission-decision JSON line terminated with '\n'.
std::string formatPermission(const std::string& promptId,
                             PermissionDecision decision);
