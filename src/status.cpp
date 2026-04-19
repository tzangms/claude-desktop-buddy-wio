#include "status.h"
#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "mem.h"
#include "state.h"
#endif

#include "persist.h"

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
  stats["appr"] = snap.appr;
  stats["deny"] = snap.deny;
  stats["vel"]  = snap.vel;
  stats["nap"]  = snap.nap;
  stats["lvl"]  = snap.lvl;

  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}

#ifdef ARDUINO
StatusSnapshot captureStatus(const AppState& /*s*/, uint32_t nowMs) {
  const PersistData& p = persistGet();
  StatusSnapshot snap;
  snap.name      = p.deviceName;
  snap.ownerName = p.ownerName;
  snap.appr      = p.appr;
  snap.deny      = p.deny;
  snap.lvl       = p.lvl;
  snap.nap       = p.nap;
  snap.vel       = p.vel;
  snap.sec       = false;
  snap.upSec     = nowMs / 1000;
  snap.heapFree  = freeHeapBytes();
  return snap;
}
#endif
