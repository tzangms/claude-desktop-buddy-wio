#pragma once

#include <cstddef>
#include <cstdint>
#include "manifest.h"   // MANIFEST_NAME_MAX

struct AppState;

static constexpr size_t CAROUSEL_MAX_CHARS = 16;
using CarouselName = char[MANIFEST_NAME_MAX + 1];

// Enumerate `/chars/` subdirectories into `out` (alphabetical order,
// capped at `max` entries). Returns count written. ARDUINO reads SFUD;
// native returns whatever `_carouselSetFakeChars` last injected.
size_t carouselEnumerate(CarouselName* out, size_t max);

// Advance the active char by one position.
//   forward=true  → next char in alphabetical order, wraps.
//   forward=false → previous char, wraps.
// Returns false if no chars are uploaded (no-op). With exactly 1 char,
// returns true and sets overlay with current name without changing
// persist/manifest state.
bool carouselAdvance(AppState& s, bool forward, uint32_t nowMs);

#ifndef ARDUINO
// Test-only: inject a list of char names for `carouselEnumerate` to
// return. Stored as a copy; caller can free `names` after calling.
void _carouselSetFakeChars(const char* const* names, size_t n);
#endif
