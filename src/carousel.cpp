#include "carousel.h"
#include "state.h"
#include "persist.h"
#include "character.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#endif

#ifndef ARDUINO
namespace {
  CarouselName fakeChars[CAROUSEL_MAX_CHARS];
  size_t       fakeCount = 0;
}

void _carouselSetFakeChars(const char* const* names, size_t n) {
  fakeCount = n < CAROUSEL_MAX_CHARS ? n : CAROUSEL_MAX_CHARS;
  for (size_t i = 0; i < fakeCount; ++i) {
    std::strncpy(fakeChars[i], names[i], MANIFEST_NAME_MAX);
    fakeChars[i][MANIFEST_NAME_MAX] = '\0';
  }
}
#endif

size_t carouselEnumerate(CarouselName* out, size_t max) {
#ifndef ARDUINO
  // Copy all available names into out (up to CAROUSEL_MAX_CHARS).
  size_t total = fakeCount;
  for (size_t i = 0; i < total; ++i) {
    std::strncpy(out[i], fakeChars[i], MANIFEST_NAME_MAX);
    out[i][MANIFEST_NAME_MAX] = '\0';
  }
  // Simple insertion sort over full list: total <= CAROUSEL_MAX_CHARS.
  for (size_t i = 1; i < total; ++i) {
    CarouselName key;
    std::strncpy(key, out[i], MANIFEST_NAME_MAX + 1);
    size_t j = i;
    while (j > 0 && std::strcmp(out[j - 1], key) > 0) {
      std::strncpy(out[j], out[j - 1], MANIFEST_NAME_MAX + 1);
      --j;
    }
    std::strncpy(out[j], key, MANIFEST_NAME_MAX + 1);
  }
  // Cap after sort so caller receives the first `max` in sorted order.
  size_t n = total < max ? total : max;
  return n;
#else
  size_t n = 0;
  if (!SFUD.exists("/chars")) return 0;
  File dir = SFUD.open("/chars");
  if (!dir) return 0;
  if (!dir.isDirectory()) { dir.close(); return 0; }

  while (n < max) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) {
      const char* name = entry.name();
      // Seeed FS returns either "charname" or "/chars/charname" depending
      // on firmware. Strip any leading path so downstream persist +
      // manifest calls see a bare name.
      const char* slash = std::strrchr(name, '/');
      const char* bare  = slash ? slash + 1 : name;
      if (bare[0] != '\0' && bare[0] != '.') {   // skip "." and ".."
        std::strncpy(out[n], bare, MANIFEST_NAME_MAX);
        out[n][MANIFEST_NAME_MAX] = '\0';
        ++n;
      }
    }
    entry.close();
  }
  dir.close();

  // Insertion sort (same as native branch above).
  for (size_t i = 1; i < n; ++i) {
    CarouselName key;
    std::strncpy(key, out[i], MANIFEST_NAME_MAX + 1);
    size_t j = i;
    while (j > 0 && std::strcmp(out[j - 1], key) > 0) {
      std::strncpy(out[j], out[j - 1], MANIFEST_NAME_MAX + 1);
      --j;
    }
    std::strncpy(out[j], key, MANIFEST_NAME_MAX + 1);
  }
  return n;
#endif
}

bool carouselAdvance(AppState& s, bool forward, uint32_t nowMs) {
  CarouselName names[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(names, CAROUSEL_MAX_CHARS);
  if (n == 0) return false;

  const char* curr = persistGetActiveChar();
  size_t idx = 0;
  for (size_t i = 0; i < n; ++i) {
    if (std::strcmp(names[i], curr) == 0) { idx = i; break; }
  }

  size_t newIdx = idx;
  if (n >= 2) {
    if (forward) newIdx = (idx + 1) % n;
    else         newIdx = (idx + n - 1) % n;

    persistSetActiveChar(names[newIdx]);
    manifestSetActive(names[newIdx]);   // native stub returns false; harmless
    characterRefreshManifest();         // native stub; Arduino closes + reopens
  }

  std::strncpy(s.buddyOverlayName, names[newIdx], MANIFEST_NAME_MAX);
  s.buddyOverlayName[MANIFEST_NAME_MAX] = '\0';
  s.buddyOverlayUntilMs = nowMs + BUDDY_OVERLAY_MS;
  return true;
}
