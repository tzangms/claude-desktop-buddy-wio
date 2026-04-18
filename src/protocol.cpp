#include "protocol.h"
#include <ArduinoJson.h>

ParsedMessage parseLine(const std::string& line) {
  ParsedMessage m;
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    m.kind = MessageKind::ParseError;
    return m;
  }

  if (doc.containsKey("total")) {
    m.kind = MessageKind::Heartbeat;
    m.heartbeat.total   = doc["total"]   | 0;
    m.heartbeat.running = doc["running"] | 0;
    m.heartbeat.waiting = doc["waiting"] | 0;
    const char* msg = doc["msg"] | "";
    m.heartbeat.msg = msg;
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
