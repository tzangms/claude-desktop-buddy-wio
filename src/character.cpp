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

  // Pure lookup: state + variant index → filename, with fallback chain.
  // Does NOT track state or time.
  const char* pickStateFile(PetState state, uint8_t vIdx) {
    const CharManifest* m = manifestActive();
    if (!m) return nullptr;

    ManifestStateIdx idx = mapState(state);
    if (m->stateVariantCount[idx] > 0) {
      uint8_t v = vIdx < m->stateVariantCount[idx] ? vIdx : 0;
      return m->states[idx][v];
    }
    // Fall back to sleep, then idle[0].
    if (m->stateVariantCount[MANIFEST_STATE_SLEEP] > 0)
      return m->states[MANIFEST_STATE_SLEEP][0];
    if (m->stateVariantCount[MANIFEST_STATE_IDLE] > 0)
      return m->states[MANIFEST_STATE_IDLE][0];
    return nullptr;
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
const char* _characterPickFile(PetState state, uint32_t nowMs) {
  // State transition → reset variant bookkeeping so a re-entry starts
  // at variant[0] with a fresh dwell window.
  if (!hasCurState || state != curState) {
    curState = state;
    variantIdx = 0;
    variantStart = nowMs;
    hasCurState = true;
  }

  // Idle-only rotation: advance variantIdx when dwell window elapses.
  // This matches the *observable* behavior of the ARDUINO decoder from
  // a caller's POV: after VARIANT_DWELL_MS, a new variant name appears.
  // The real decoder only advances at end-of-animation, but since tests
  // exercise by-name output and the dwell is a floor not a ceiling,
  // time-based here is accurate enough for the test contract.
  if (state == PetState::Idle) {
    const CharManifest* m = manifestActive();
    if (m) {
      uint8_t n = m->stateVariantCount[MANIFEST_STATE_IDLE];
      if (n > 1 && (nowMs - variantStart) >= VARIANT_DWELL_MS) {
        uint32_t elapsed = nowMs - variantStart;
        uint32_t steps   = elapsed / VARIANT_DWELL_MS;
        variantIdx = (uint8_t)((variantIdx + steps) % n);
        variantStart += steps * VARIANT_DWELL_MS;
      }
    }
  }

  uint8_t vIdx = (state == PetState::Idle) ? variantIdx : 0;
  return pickStateFile(state, vIdx);
}

void _characterResetForTest() {
  curState = PetState::Sleep;
  variantIdx = 0;
  variantStart = 0;
  hasCurState = false;
}
#endif
