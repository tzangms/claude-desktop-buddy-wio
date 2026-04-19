#include "character.h"
#include "manifest.h"
#include "config.h"

#include <cstring>
#include <cstdio>

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
  // Forward declarations — callback definitions come next, but openVariant
  // (below the draw callback) and characterInit reference them.
  AnimatedGIF gif;
  File      gifFile;
  bool      ready       = false;
  bool      gifOpen     = false;
  uint32_t  nextFrameAt = 0;
  uint32_t  animPauseUntil = 0;

  // AnimatedGIF wants 4 callbacks to talk to a file system. Each wraps
  // a Seeed_Arduino_FS File* passed as handle.
  void* gifOpenCb(const char* fname, int32_t* pSize) {
    gifFile = SFUD.open(fname, FILE_READ);
    if (!gifFile) return nullptr;
    *pSize = (int32_t)gifFile.size();
    return (void*)&gifFile;
  }

  void gifCloseCb(void* handle) {
    File* f = (File*)handle;
    if (f && *f) f->close();
  }

  int32_t gifReadCb(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = (File*)pFile->fHandle;
    int32_t n = f->read(pBuf, iLen);
    pFile->iPos = (int32_t)f->position();
    return n;
  }

  int32_t gifSeekCb(GIFFILE* pFile, int32_t iPosition) {
    File* f = (File*)pFile->fHandle;
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    return pFile->iPos;
  }

  int gifDrawX = 0, gifDrawY = 0;

  // Called by AnimatedGIF once per decoded scanline. Writes RGB565
  // pixels directly to the TFT — no framebuffer / sprite.
  // Transparent pixels paint manifest bg color so each frame fully
  // repaints its rect and we get no ghosting.
  void gifDrawCb(GIFDRAW* d) {
    uint16_t* pal16 = d->pPalette;
    uint8_t*  src   = d->pPixels;
    uint8_t   tr    = d->ucTransparent;
    bool      hasT  = d->ucHasTransparency;

    const CharManifest* m = manifestActive();
    uint16_t bg = m ? m->colorBg : 0x0000;

    int y = gifDrawY + d->iY + d->y;
    if (y < 0 || y >= SCREEN_H) return;
    int x0 = gifDrawX + d->iX;
    int w  = d->iWidth;
    if (x0 < 0) { src -= x0; w += x0; x0 = 0; }
    if (x0 + w > SCREEN_W) w = SCREEN_W - x0;
    if (w <= 0) return;

    for (int i = 0; i < w; i++) {
      uint8_t idx = src[i];
      uint16_t c = (hasT && idx == tr) ? bg : pal16[idx];
      tft.drawPixel(x0 + i, y, c);
    }
  }

  bool openVariant(PetState state, uint8_t vIdx, uint32_t nowMs) {
    const char* fn = pickStateFile(state, vIdx);
    if (!fn) return false;
    const CharManifest* m = manifestActive();
    if (!m) return false;
    char path[96];
    std::snprintf(path, sizeof(path), "/chars/%s/%s", m->name, fn);
    if (!gif.open(path, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) {
      Serial.print("[char] open failed: ");
      Serial.println(path);
      return false;
    }
    gifOpen = true;
    gifDrawX = BUDDY_X;
    gifDrawY = BUDDY_Y;
    nextFrameAt = 0;
    variantStart = nowMs;
    return true;
  }
#endif
}

// --- Public API ---

#ifdef ARDUINO
void characterInit() {
  ready = false;
  const CharManifest* m = manifestActive();
  if (!m) return;

  // Validate first-variant file for each populated state opens as a GIF.
  // Header-only: open → close. Cheap.
  gif.begin(LITTLE_ENDIAN_PIXELS);
  bool anyOk = false;
  for (int i = 0; i < MANIFEST_STATE_COUNT; ++i) {
    if (m->stateVariantCount[i] == 0) continue;
    char path[96];
    std::snprintf(path, sizeof(path), "/chars/%s/%s",
                  m->name, m->states[i][0]);
    if (!gif.open(path, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) {
      Serial.print("[char] bad gif: ");
      Serial.println(path);
      continue;
    }
    gif.close();
    anyOk = true;
  }

  // As long as manifestActive() is non-null AND at least one state file
  // validated, we're ready. pickFileImpl handles per-state misses.
  ready = anyOk;
  if (ready) {
    Serial.print("[char] ready: ");
    Serial.println(m->name);
  } else {
    Serial.println("[char] init: no valid gifs, falling back to ASCII");
  }
}
#else
void characterInit() {}
#endif

bool characterReady() {
#ifdef ARDUINO
  return ready;
#else
  return false;
#endif
}

#ifdef ARDUINO
void characterSetState(PetState state) {
  if (!ready) return;
  if (hasCurState && state == curState && gifOpen) return;

  if (gifOpen) { gif.close(); gifOpen = false; }
  animPauseUntil = 0;

  // New state entry always starts at variant 0.
  curState = state;
  variantIdx = 0;
  hasCurState = true;
  openVariant(state, 0, millis());
}
#else
void characterSetState(PetState) {}
#endif

#ifdef ARDUINO
void characterTick(uint32_t nowMs) {
  if (!ready) return;

  // Between-variant pause: hold the last frame (decoder already closed
  // + variantIdx already advanced when we entered the pause), then open
  // the next variant when the pause window elapses.
  if (!gifOpen) {
    if (animPauseUntil && nowMs >= animPauseUntil) {
      animPauseUntil = 0;
      if (hasCurState) openVariant(curState, variantIdx, nowMs);
    }
    return;
  }

  if (nowMs < nextFrameAt) return;

  int delayMs = 0;
  if (!gif.playFrame(false, &delayMs)) {
    // End of animation.
    const CharManifest* m = manifestActive();
    uint8_t n = m ? m->stateVariantCount[mapState(curState)] : 0;

    if (n <= 1) {
      // Single-variant state: freeze on last frame. Stop ticking the
      // decoder to avoid restart overhead / SFUD thrash.
      gif.close();
      gifOpen = false;
      return;
    }
    // Multi-variant (idle): loop this GIF until dwell elapses, then
    // advance variantIdx and pause. Next tick after the pause opens the
    // new variant (see the top of this function).
    if ((nowMs - variantStart) < VARIANT_DWELL_MS) {
      gif.reset();
      nextFrameAt = nowMs;
      return;
    }
    gif.close();
    gifOpen = false;
    variantIdx = (uint8_t)((variantIdx + 1) % n);
    animPauseUntil = nowMs + ANIM_PAUSE_MS;
    return;
  }
  nextFrameAt = nowMs + (delayMs > 0 ? delayMs : 100);
}
#else
void characterTick(uint32_t) {}
#endif

#ifdef ARDUINO
void characterInvalidate() {
  if (gifOpen) { gif.close(); gifOpen = false; }
  animPauseUntil = 0;
  nextFrameAt = 0;
  // Next characterTick (after characterSetState) reopens and repaints.
}
#else
void characterInvalidate() {}
#endif

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
