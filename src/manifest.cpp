#include "manifest.h"

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#endif

namespace {
  int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool parseHex24(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!hex) return false;
    if (hex[0] != '#') return false;
    for (int i = 1; i < 7; ++i) if (hexDigit(hex[i]) < 0) return false;
    if (hex[7] != '\0') return false;
    r = (hexDigit(hex[1]) << 4) | hexDigit(hex[2]);
    g = (hexDigit(hex[3]) << 4) | hexDigit(hex[4]);
    b = (hexDigit(hex[5]) << 4) | hexDigit(hex[6]);
    return true;
  }

  uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) |
           ((uint16_t)(g & 0xFC) << 3) |
           ((uint16_t)(b & 0xF8) >> 3);
  }

  bool readRequiredColor(JsonObjectConst colors, const char* key,
                         uint16_t& out, std::string& err) {
    if (!colors.containsKey(key)) {
      err = std::string("colors.") + key + " missing";
      return false;
    }
    const char* hex = colors[key] | "";
    uint8_t r, g, b;
    if (!parseHex24(hex, r, g, b)) {
      err = std::string("colors.") + key + " not #RRGGBB";
      return false;
    }
    out = rgb565(r, g, b);
    return true;
  }

  bool hasActive = false;
  CharManifest active;

  struct StateName { const char* key; ManifestStateIdx idx; };
  constexpr StateName kStateNames[] = {
    {"sleep",     MANIFEST_STATE_SLEEP},
    {"idle",      MANIFEST_STATE_IDLE},
    {"busy",      MANIFEST_STATE_BUSY},
    {"attention", MANIFEST_STATE_ATTENTION},
    {"celebrate", MANIFEST_STATE_CELEBRATE},
    {"heart",     MANIFEST_STATE_HEART},
    {"dizzy",     MANIFEST_STATE_DIZZY},
    {"nap",       MANIFEST_STATE_NAP},
  };

  void storeVariant(CharManifest& out, ManifestStateIdx idx, const char* fn) {
    uint8_t& n = out.stateVariantCount[idx];
    if (n >= MANIFEST_MAX_VARIANTS) return;
    std::strncpy(out.states[idx][n], fn, MANIFEST_FILENAME_MAX);
    out.states[idx][n][MANIFEST_FILENAME_MAX] = '\0';
    ++n;
  }

  void parseStates(JsonObjectConst states, CharManifest& out, std::string& err) {
    for (const auto& sn : kStateNames) {
      if (!states.containsKey(sn.key)) continue;
      JsonVariantConst v = states[sn.key];
      if (v.is<const char*>()) {
        storeVariant(out, sn.idx, v.as<const char*>());
      } else if (v.is<JsonArrayConst>()) {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        size_t seen = 0;
        for (JsonVariantConst f : arr) {
          const char* fn = f | "";
          if (fn[0] == '\0') continue;
          if (out.stateVariantCount[sn.idx] < MANIFEST_MAX_VARIANTS) {
            storeVariant(out, sn.idx, fn);
          }
          ++seen;
        }
        if (seen > MANIFEST_MAX_VARIANTS && err.empty()) {
          err = std::string("states.") + sn.key + " truncated";
        }
      }
      // Unknown types ignored silently.
    }
  }
}

#ifndef ARDUINO
uint16_t _manifestHex24ToRgb565(const char* hex) {
  uint8_t r, g, b;
  if (!parseHex24(hex, r, g, b)) return 0;
  return rgb565(r, g, b);
}
#endif

bool manifestParseJson(const char* json, size_t len,
                       CharManifest& out, std::string& err) {
  err.clear();
  std::memset(&out, 0, sizeof(out));

  DynamicJsonDocument doc(4096);
  DeserializationError de = deserializeJson(doc, json, len);
  if (de) { err = de.c_str(); return false; }

  JsonObjectConst root = doc.as<JsonObjectConst>();

  const char* name = root["name"] | "";
  if (name[0] == '\0') { err = "name missing"; return false; }
  std::strncpy(out.name, name, MANIFEST_NAME_MAX);
  out.name[MANIFEST_NAME_MAX] = '\0';

  if (!root.containsKey("colors") || !root["colors"].is<JsonObjectConst>()) {
    err = "colors missing"; return false;
  }
  JsonObjectConst colors = root["colors"].as<JsonObjectConst>();
  if (!readRequiredColor(colors, "body",    out.colorBody,    err)) return false;
  if (!readRequiredColor(colors, "bg",      out.colorBg,      err)) return false;
  if (!readRequiredColor(colors, "text",    out.colorText,    err)) return false;
  if (!readRequiredColor(colors, "textDim", out.colorTextDim, err)) return false;
  if (!readRequiredColor(colors, "ink",     out.colorInk,     err)) return false;

  if (root.containsKey("states") && root["states"].is<JsonObjectConst>()) {
    parseStates(root["states"].as<JsonObjectConst>(), out, err);
  }
  return true;
}

#ifdef ARDUINO
bool manifestParseFile(const char* path, CharManifest& out, std::string& err) {
  File f = SFUD.open(path, FILE_READ);
  if (!f) { err = "open failed"; return false; }
  size_t size = f.size();
  if (size == 0 || size > 8192) {
    f.close(); err = "size out of range"; return false;
  }
  // Read into a heap buffer; stack is tight next to rpcBLE.
  std::string buf;
  buf.resize(size);
  size_t n = f.read(reinterpret_cast<uint8_t*>(&buf[0]), size);
  f.close();
  if (n != size) { err = "short read"; return false; }
  return manifestParseJson(buf.data(), buf.size(), out, err);
}

bool manifestSetActive(const char* charName) {
  if (!charName || !charName[0]) return false;
  char path[96];
  std::snprintf(path, sizeof(path), "/chars/%s/manifest.json", charName);
  CharManifest staging;
  std::string err;
  if (!manifestParseFile(path, staging, err)) return false;
  active = staging;
  hasActive = true;
  return true;
}
#else
bool manifestSetActive(const char*) { return false; }
#endif

const CharManifest* manifestActive() {
  return hasActive ? &active : nullptr;
}

#ifndef ARDUINO
void _manifestResetForTest() {
  hasActive = false;
  std::memset(&active, 0, sizeof(active));
}
bool _manifestSetActiveFromJson(const char* json, size_t len) {
  CharManifest staging;
  std::string err;
  if (!manifestParseJson(json, len, staging, err)) return false;
  active = staging;
  hasActive = true;
  return true;
}
#endif
