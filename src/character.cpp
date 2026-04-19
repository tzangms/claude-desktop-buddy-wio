#include "character.h"
#include "manifest.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
#include <AnimatedGIF.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#include <TFT_eSPI.h>
extern TFT_eSPI tft;  // defined in ui.cpp
#endif

namespace {
  // Runtime state for pick-file logic (stateful — advances variant index
  // after VARIANT_DWELL_MS elapsed since variant started).
  PetState  curState      = PetState::Sleep;
  uint8_t   variantIdx    = 0;
  uint32_t  variantStart  = 0;
  bool      hasCurState   = false;   // false at boot; first pick sets it.

  // Mapping PetState → ManifestStateIdx matches SP6a's manifest enum.
  ManifestStateIdx mapState(PetState s) {
    switch (s) {
      case PetState::Sleep:     return MANIFEST_STATE_SLEEP;
      case PetState::Idle:      return MANIFEST_STATE_IDLE;
      case PetState::Busy:      return MANIFEST_STATE_BUSY;
      case PetState::Attention: return MANIFEST_STATE_ATTENTION;
      case PetState::Celebrate: return MANIFEST_STATE_CELEBRATE;
      case PetState::Heart:     return MANIFEST_STATE_HEART;
      case PetState::Dizzy:     return MANIFEST_STATE_DIZZY;
      case PetState::Nap:       return MANIFEST_STATE_NAP;
    }
    return MANIFEST_STATE_SLEEP;
  }

#ifdef ARDUINO
  bool      ready       = false;
  AnimatedGIF gif;
  File      gifFile;
  bool      gifOpen     = false;
  uint32_t  nextFrameAt = 0;
  uint32_t  animPauseUntil = 0;
#endif
}

// --- Public API stubs (filled in by later tasks) ---

void characterInit() {
  // Implemented in Task 3.
}

bool characterReady() {
#ifdef ARDUINO
  return ready;
#else
  return false;
#endif
}

void characterSetState(PetState) {
  // Implemented in Task 3.
}

void characterTick(uint32_t) {
  // Implemented in Task 3.
}

void characterInvalidate() {
  // Implemented in Task 3.
}

#ifndef ARDUINO
const char* _characterPickFile(PetState, uint32_t) {
  // Implemented in Task 2.
  return nullptr;
}

void _characterResetForTest() {
  curState = PetState::Sleep;
  variantIdx = 0;
  variantStart = 0;
  hasCurState = false;
}
#endif
