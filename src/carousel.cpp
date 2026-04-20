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

size_t carouselEnumerate(CarouselName* /*out*/, size_t /*max*/) {
  return 0;   // TDD placeholder; real impl lands in Task 2.
}

bool carouselAdvance(AppState& /*s*/, bool /*forward*/, uint32_t /*nowMs*/) {
  return false;  // TDD placeholder; real impl lands in Task 3.
}
