#include "manifest.h"

#include <cstring>

#include <ArduinoJson.h>

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
}

uint16_t _manifestHex24ToRgb565(const char* hex) {
  uint8_t r, g, b;
  if (!parseHex24(hex, r, g, b)) return 0;
  return rgb565(r, g, b);
}

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

  // states parsed in Task 4.
  return true;
}

bool manifestSetActive(const char*) { return false; }

const CharManifest* manifestActive() {
  return hasActive ? &active : nullptr;
}

#ifndef ARDUINO
void _manifestResetForTest() {
  hasActive = false;
  std::memset(&active, 0, sizeof(active));
}
bool _manifestSetActiveFromJson(const char* json, size_t len) {
  std::string err;
  if (!manifestParseJson(json, len, active, err)) return false;
  hasActive = true;
  return true;
}
#endif
