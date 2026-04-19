#include "status.h"
#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "mem.h"
#include "state.h"
#endif

// ArduinoJson v6 internal memory pool; <512 overflows the nested tree.
static constexpr size_t STATUS_DOC_POOL = 1024;

std::string formatStatusAck(const StatusSnapshot& snap) {
  StaticJsonDocument<STATUS_DOC_POOL> doc;
  doc["ack"] = "status";
  doc["ok"] = true;
  JsonObject data = doc.createNestedObject("data");
  data["name"] = snap.name;
  data["sec"] = snap.sec;

  JsonObject bat = data.createNestedObject("bat");
  bat["pct"] = 100;
  bat["mV"]  = 5000;
  bat["mA"]  = 0;
  bat["usb"] = true;

  JsonObject sys = data.createNestedObject("sys");
  sys["up"]   = snap.upSec;
  sys["heap"] = snap.heapFree;

  JsonObject stats = data.createNestedObject("stats");
  stats["appr"] = 0;
  stats["deny"] = 0;
  stats["vel"]  = 0;
  stats["nap"]  = 0;
  stats["lvl"]  = 0;

  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}

#ifdef ARDUINO
StatusSnapshot captureStatus(const AppState& s, uint32_t nowMs) {
  StatusSnapshot snap;
  snap.name = s.deviceName;
  snap.sec = false;
  snap.upSec = nowMs / 1000;
  snap.heapFree = freeHeapBytes();
  return snap;
}
#endif
