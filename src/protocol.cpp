#include "protocol.h"
#include "config.h"
#include <ArduinoJson.h>

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
    const char* msg = doc["msg"] | "";
    m.heartbeat.msg = msg;
    if (doc.containsKey("entries") && doc["entries"].is<JsonArray>()) {
      JsonArray arr = doc["entries"].as<JsonArray>();
      size_t n = 0;
      for (JsonVariant v : arr) {
        if (n >= ENTRIES_MAX) break;
        const char* s = v | "";
        std::string entry(s);
        if (entry.size() > ENTRY_CHARS_MAX) entry.resize(ENTRY_CHARS_MAX);
        m.heartbeat.entries.push_back(std::move(entry));
        ++n;
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

  if (doc["cmd"] == "owner") {
    m.kind = MessageKind::Owner;
    m.ownerName = doc["name"] | "";
    return m;
  }

  if (doc["evt"] == "turn") {
    m.kind = MessageKind::TurnEvent;
    return m;
  }

  if (doc["cmd"] == "status") {
    m.kind = MessageKind::StatusCmd;
    return m;
  }

  if (doc["cmd"] == "unpair") {
    m.kind = MessageKind::UnpairCmd;
    return m;
  }

  if (doc["cmd"] == "name") {
    m.kind = MessageKind::NameCmd;
    m.nameValue = doc["name"] | "";
    return m;
  }

  if (doc.containsKey("time") && doc["time"].is<JsonArray>()) {
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
