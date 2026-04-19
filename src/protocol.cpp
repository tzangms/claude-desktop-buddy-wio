#include "protocol.h"
#include "config.h"
#include <ArduinoJson.h>
#include <cstring>

ParsedMessage parseLine(const std::string& line) {
  ParsedMessage m;
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    m.kind = MessageKind::ParseError;
    return m;
  }

  if (doc.containsKey("total") && !doc.containsKey("cmd")) {
    m.kind = MessageKind::Heartbeat;
    m.heartbeat.total   = doc["total"]   | 0;
    m.heartbeat.running = doc["running"] | 0;
    m.heartbeat.waiting = doc["waiting"] | 0;
    m.heartbeat.msg = doc["msg"] | "";
    if (doc["entries"].is<JsonArray>()) {
      m.heartbeat.entries.reserve(ENTRIES_MAX);
      for (JsonVariant v : doc["entries"].as<JsonArray>()) {
        if (m.heartbeat.entries.size() >= ENTRIES_MAX) break;
        const char* s = v | "";
        m.heartbeat.entries.emplace_back(s, strnlen(s, ENTRY_CHARS_MAX));
      }
    }
    m.heartbeat.tokens       = doc["tokens"]       | (int64_t)0;
    m.heartbeat.tokens_today = doc["tokens_today"] | (int64_t)0;
    if (doc.containsKey("prompt") && !doc["prompt"].isNull()) {
      m.heartbeat.hasPrompt = true;
      m.heartbeat.prompt.id   = doc["prompt"]["id"]   | "";
      m.heartbeat.prompt.tool = doc["prompt"]["tool"] | "";
      m.heartbeat.prompt.hint = doc["prompt"]["hint"] | "";
    }
    return m;
  }

  const char* cmd = doc["cmd"] | "";
  const char* evt = doc["evt"] | "";

  if (!strcmp(cmd, "owner")) {
    m.kind = MessageKind::Owner;
    m.ownerName = doc["name"] | "";
    return m;
  }
  if (!strcmp(evt, "turn")) {
    m.kind = MessageKind::TurnEvent;
    return m;
  }
  if (!strcmp(cmd, "status")) {
    m.kind = MessageKind::StatusCmd;
    return m;
  }
  if (!strcmp(cmd, "unpair")) {
    m.kind = MessageKind::UnpairCmd;
    return m;
  }
  if (!strcmp(cmd, "name")) {
    m.kind = MessageKind::NameCmd;
    m.nameValue = doc["name"] | "";
    return m;
  }
  if (!strcmp(cmd, "char_begin")) {
    m.kind = MessageKind::CharBegin;
    m.xferName  = doc["name"]  | "";
    m.xferTotal = doc["total"] | (int64_t)0;
    return m;
  }
  if (!strcmp(cmd, "file")) {
    m.kind = MessageKind::FileBegin;
    m.xferPath = doc["path"] | "";
    m.xferSize = doc["size"] | (int64_t)0;
    return m;
  }
  if (!strcmp(cmd, "chunk")) {
    m.kind = MessageKind::Chunk;
    m.xferChunk = doc["d"] | "";
    return m;
  }
  if (!strcmp(cmd, "file_end")) {
    m.kind = MessageKind::FileEnd;
    return m;
  }
  if (!strcmp(cmd, "char_end")) {
    m.kind = MessageKind::CharEnd;
    return m;
  }

  if (doc["time"].is<JsonArray>()) {
    JsonArray a = doc["time"].as<JsonArray>();
    if (a.size() >= 2) {
      m.kind = MessageKind::Time;
      m.timeEpoch     = a[0] | 0;
      m.timeOffsetSec = a[1] | 0;
      return m;
    }
  }

  m.kind = MessageKind::Unknown;
  return m;
}

std::string formatPermission(const std::string& promptId,
                             PermissionDecision decision) {
  StaticJsonDocument<256> doc;
  doc["cmd"] = "permission";
  doc["id"] = promptId;
  doc["decision"] = (decision == PermissionDecision::Approve) ? "once" : "deny";
  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}

std::string formatAck(const std::string& cmd, bool ok,
                      const std::string& error) {
  StaticJsonDocument<256> doc;
  doc["ack"] = cmd;
  doc["ok"] = ok;
  if (!error.empty()) doc["error"] = error;
  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}

std::string formatAckN(const std::string& cmd, bool ok, int64_t n,
                       const std::string& error) {
  StaticJsonDocument<256> doc;
  doc["ack"] = cmd;
  doc["ok"] = ok;
  doc["n"] = n;
  if (!error.empty()) doc["error"] = error;
  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}
