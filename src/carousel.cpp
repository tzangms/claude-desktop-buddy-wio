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
  (void)out; (void)max;
  return 0;   // Task 8 fills in the SFUD-backed version.
#endif
}

bool carouselAdvance(AppState& /*s*/, bool /*forward*/, uint32_t /*nowMs*/) {
  return false;  // TDD placeholder; real impl lands in Task 3.
}
