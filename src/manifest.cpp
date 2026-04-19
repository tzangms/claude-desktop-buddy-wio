#include "manifest.h"

#include <cstring>

namespace {
  bool hasActive = false;
  CharManifest active;
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
