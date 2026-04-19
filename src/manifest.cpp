#include "manifest.h"

#include <cstring>

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

  bool hasActive = false;
  CharManifest active;
}

uint16_t _manifestHex24ToRgb565(const char* hex) {
  uint8_t r, g, b;
  if (!parseHex24(hex, r, g, b)) return 0;
  return rgb565(r, g, b);
}

bool manifestParseJson(const char*, size_t, CharManifest&, std::string& err) {
  err = "not implemented";
  return false;
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
