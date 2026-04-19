#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Sized for bufo (9 idle variants). Raise if a real manifest needs more.
static constexpr size_t MANIFEST_MAX_VARIANTS  = 16;
static constexpr size_t MANIFEST_FILENAME_MAX  = 32;
static constexpr size_t MANIFEST_NAME_MAX      = 32;

// Indices match src/pet.h::PetState enum ordering. Hard-coded so tests
// and manifest.cpp agree without pulling pet.h into manifest.h.
enum ManifestStateIdx : uint8_t {
  MANIFEST_STATE_SLEEP = 0,
  MANIFEST_STATE_IDLE,
  MANIFEST_STATE_BUSY,
  MANIFEST_STATE_ATTENTION,
  MANIFEST_STATE_CELEBRATE,
  MANIFEST_STATE_HEART,
  MANIFEST_STATE_DIZZY,
  MANIFEST_STATE_NAP,
  MANIFEST_STATE_COUNT,
};

struct CharManifest {
  char     name[MANIFEST_NAME_MAX + 1];
  uint16_t colorBody;     // RGB565
  uint16_t colorBg;
  uint16_t colorText;
  uint16_t colorTextDim;
  uint16_t colorInk;
  uint8_t  stateVariantCount[MANIFEST_STATE_COUNT];
  char     states[MANIFEST_STATE_COUNT]
                 [MANIFEST_MAX_VARIANTS]
                 [MANIFEST_FILENAME_MAX + 1];
};

// Parse a JSON blob into `out`. Returns true on success; sets `err` on
// failure or non-fatal warning (e.g. variant array truncation still
// returns true with err set).
bool manifestParseJson(const char* json, size_t len,
                       CharManifest& out, std::string& err);

// Read "/chars/{charName}/manifest.json" from SFUD, parse, cache as
// active. On failure, active cache is unchanged.
bool manifestSetActive(const char* charName);

// Currently cached manifest, or nullptr if none.
const CharManifest* manifestActive();

#ifndef ARDUINO
// Test-only hooks.
void _manifestResetForTest();
bool _manifestSetActiveFromJson(const char* json, size_t len);
#endif
