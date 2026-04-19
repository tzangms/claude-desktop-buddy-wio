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
  CharBegin,
  FileBegin,
  Chunk,
  FileEnd,
  CharEnd,
  Unknown,
  ParseError,
};

struct ParsedMessage {
  MessageKind kind = MessageKind::Unknown;
  HeartbeatData heartbeat;
  std::string ownerName;
  std::string nameValue;
  int32_t timeEpoch = 0;
  int32_t timeOffsetSec = 0;

  // Folder-push fields.
  std::string xferName;          // char_begin name
  int64_t     xferTotal = 0;     // char_begin total bytes
  std::string xferPath;          // file path
  int64_t     xferSize = 0;      // file expected size
  std::string xferChunk;         // base64 chunk body
};

// Format an ack that includes a numeric "n" counter (for chunk/file_end).
std::string formatAckN(const std::string& cmd, bool ok, int64_t n,
                       const std::string& error = "");

// Parse one line of JSON (already stripped of trailing '\n').
ParsedMessage parseLine(const std::string& line);

// Build a permission-decision JSON line terminated with '\n'.
std::string formatPermission(const std::string& promptId,
                             PermissionDecision decision);

// Build a generic ack JSON line terminated with '\n'.
// If `error` is non-empty, it's included as `"error": "..."`.
std::string formatAck(const std::string& cmd, bool ok,
                      const std::string& error = "");
