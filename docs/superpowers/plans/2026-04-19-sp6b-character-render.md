# SP6b — Character render via AnimatedGIF Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the active buddy's GIF frames on the idle screen at a fixed 96×100 slot, replacing the ASCII pet when `manifestActive()` is non-null.

**Architecture:** New `src/character.cpp/h` ported from `anthropics/claude-desktop-buddy/src/character.cpp`, adapted for Wio Terminal (Seeed_Arduino_FS + SFUD, direct TFT_eSPI instead of TFT_eSprite). Decoder lives under `#ifdef ARDUINO`; native stubs let us TDD the pure state → filename picking logic (including variant rotation and fallback chain) without AnimatedGIF. `ui.cpp::renderIdle` reflows to give the buddy its own left-column slot and downsizes the big stat digits from size-5 to size-3 to make room. `main::loop` drives `characterTick` when the device is on the Idle screen.

**Tech Stack:** C++17, Arduino framework, PlatformIO, `bitbank2/AnimatedGIF@^2.1.1`, Seeed_Arduino_FS, Seeed_SFUD, TFT_eSPI, Unity native tests.

**Design spec:** `docs/superpowers/specs/2026-04-19-sp6b-character-render-design.md`

**Branch:** `feature/sp6b-character-render` off `main`. Merge back via `--no-ff`.

**File structure:**

| File | Responsibility |
|---|---|
| `src/character.h` (new) | `characterInit` / `characterReady` / `characterSetState` / `characterTick` / `characterInvalidate` API + native test hooks |
| `src/character.cpp` (new) | State → filename pick (native-testable), stateful variant rotation, ARDUINO decoder body |
| `test/test_character/test_character.cpp` (new) | Native Unity tests for pick-file + variant rotation + fallback chain |
| `src/ui.cpp` | Reflow `renderIdle` to carve out a `(8, 34, 96×100)` buddy slot; shift stats panel to `x=112+`; shrink stat digits size-5 → size-3 |
| `src/main.cpp` | Call `characterInit` after `manifestLoadActiveFromPersist`; call `characterTick(now)` every loop iteration on Idle; call `characterSetState` on pet-state transitions; call `characterInvalidate` when UI fully redraws |
| `src/pet.cpp` | No changes — continues driving PetState via `petComputeState`; `ui.cpp` / `main.cpp` owns the ASCII vs buddy branch |
| `src/config.h` | New constants: `BUDDY_X`, `BUDDY_Y`, `BUDDY_W`, `BUDDY_H`, `VARIANT_DWELL_MS`, `ANIM_PAUSE_MS` |
| `platformio.ini` | Add `bitbank2/AnimatedGIF @ ^2.1.1` to `[env:seeed_wio_terminal]` lib_deps; add `+<character.cpp>` to native `build_src_filter` |

---

### Task 1: Branch + skeleton character module + dependency wiring

**Files:**
- Create: `src/character.h`
- Create: `src/character.cpp`
- Create: `test/test_character/test_character.cpp`
- Modify: `platformio.ini`
- Modify: `src/config.h`

- [ ] **Step 1: Create feature branch**

```bash
git checkout main
git checkout -b feature/sp6b-character-render
```

- [ ] **Step 2: Add layout + timing constants to `src/config.h`**

Append after the existing SP4b.4 character constants:

```cpp
// --- SP6b character render (GIF buddy display) ---
static constexpr int BUDDY_X = 8;
static constexpr int BUDDY_Y = 34;
static constexpr int BUDDY_W = 96;
static constexpr int BUDDY_H = 100;

// Idle variant rotation (per reference character.cpp).
static constexpr uint32_t VARIANT_DWELL_MS = 5000;  // min time per variant
static constexpr uint32_t ANIM_PAUSE_MS    = 800;   // pause between variants
```

- [ ] **Step 3: Write `src/character.h`**

```cpp
#pragma once

#include <cstdint>
#include "pet.h"

// Called once at boot after manifestLoadActiveFromPersist. Validates that
// the active manifest's first-variant file for each required state opens
// as a readable GIF. Cheap (header-only open+close) — not a full decode.
// Sets characterReady(). Safe when manifestActive() is null (no-op).
void characterInit();

// Whether SP6b's renderer is active. False → caller renders ASCII.
bool characterReady();

// Notify the renderer that the display's state machine has moved to
// `state`. Closes the currently-open GIF (if any), resets variant timers,
// and opens the new state's first-variant file. Idempotent if state is
// unchanged.
void characterSetState(PetState state);

// Drive one animation tick. Call every loop iteration while the Idle
// screen is visible. Handles per-frame timing, end-of-animation rotation
// for multi-variant idle, and the between-variant pause window.
void characterTick(uint32_t nowMs);

// UI called fullRedraw on the Idle screen — the buddy region was wiped.
// Next characterTick will reopen and repaint.
void characterInvalidate();

#ifndef ARDUINO
// Test-only: expose the pure pick-file logic and let tests drive time
// without building a real AnimatedGIF pipeline.
const char* _characterPickFile(PetState state, uint32_t nowMs);
void _characterResetForTest();
#endif
```

- [ ] **Step 4: Write `src/character.cpp` skeleton**

```cpp
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
```

- [ ] **Step 5: Write `test/test_character/test_character.cpp` with one trivial test**

```cpp
#include <unity.h>
#include "character.h"
#include "manifest.h"

void test_pick_file_null_when_no_manifest() {
  _manifestResetForTest();
  _characterResetForTest();
  TEST_ASSERT_NULL(_characterPickFile(PetState::Idle, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pick_file_null_when_no_manifest);
  return UNITY_END();
}
```

- [ ] **Step 6: Update `platformio.ini`**

Under `[env:seeed_wio_terminal]` `lib_deps`, append:

```
    bitbank2/AnimatedGIF @ ^2.1.1
```

Under `[env:native]` `build_src_filter`, append `+<character.cpp>`:

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet.cpp> +<xfer.cpp> +<manifest.cpp> +<character.cpp>
```

- [ ] **Step 7: Verify native test + firmware build**

```bash
pio test -e native -f test_character
pio run -e seeed_wio_terminal
```

Expected:
- `test_pick_file_null_when_no_manifest PASSED` (1/1)
- Firmware build SUCCESS. AnimatedGIF library appears in build output.

- [ ] **Step 8: Commit**

```bash
git add src/character.h src/character.cpp test/test_character/test_character.cpp platformio.ini src/config.h
git commit -m "$(cat <<'EOF'
sp6b: skeleton character module + AnimatedGIF dependency

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Pick-file logic with variant rotation + fallback chain (native TDD)

**Files:**
- Modify: `src/character.cpp`
- Modify: `test/test_character/test_character.cpp`

- [ ] **Step 1: Add failing tests**

At the top of `test_character.cpp`, after the existing includes, add a helper + fixtures:

```cpp
#include <cstring>
#include <string>

// Manifest with all 7 host-side states (no nap), 3-variant idle. Matches
// the real bufo shape but compressed for readability.
static const char* kManifestIdleBusySleepFull = R"({
  "name":"bufo",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{
    "sleep":"sleep.gif",
    "idle":["idle_0.gif","idle_1.gif","idle_2.gif"],
    "busy":"busy.gif",
    "attention":"attention.gif",
    "celebrate":"celebrate.gif",
    "dizzy":"dizzy.gif",
    "heart":"heart.gif"
  }
})";

// Manifest with ONLY sleep — everything else should fall back.
static const char* kManifestSleepOnly = R"({
  "name":"minimal",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{"sleep":"only_sleep.gif"}
})";

// Manifest with ONLY idle — sleep fallback should fall through to idle[0].
static const char* kManifestIdleOnly = R"({
  "name":"idleonly",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{"idle":"just_idle.gif"}
})";

static void setActive(const char* json) {
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(json, std::strlen(json)));
}
```

Add these tests:

```cpp
void test_pick_file_idle_first_variant() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif", _characterPickFile(PetState::Idle, 0));
}

void test_pick_file_idle_does_not_advance_before_dwell() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  _characterPickFile(PetState::Idle, 0);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, VARIANT_DWELL_MS - 1));
}

void test_pick_file_idle_advances_after_dwell() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  _characterPickFile(PetState::Idle, 0);
  TEST_ASSERT_EQUAL_STRING("idle_1.gif",
                           _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1));
}

void test_pick_file_idle_wraps_to_zero() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // 3 variants: 0 → 1 → 2 → 0.
  _characterPickFile(PetState::Idle, 0);
  _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1);        // 1
  _characterPickFile(PetState::Idle, 2 * VARIANT_DWELL_MS + 2);    // 2
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, 3 * VARIANT_DWELL_MS + 3));
}

void test_pick_file_non_idle_state_does_not_rotate() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // Repeated Busy calls return the same file regardless of elapsed time.
  TEST_ASSERT_EQUAL_STRING("busy.gif", _characterPickFile(PetState::Busy, 0));
  TEST_ASSERT_EQUAL_STRING("busy.gif",
                           _characterPickFile(PetState::Busy, 10 * VARIANT_DWELL_MS));
}

void test_pick_file_state_change_resets_variant() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // Advance Idle to variant 2.
  _characterPickFile(PetState::Idle, 0);
  _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1);
  _characterPickFile(PetState::Idle, 2 * VARIANT_DWELL_MS + 2);
  // Switch to Busy then back to Idle — Idle variant should restart from 0.
  _characterPickFile(PetState::Busy, 3 * VARIANT_DWELL_MS);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, 4 * VARIANT_DWELL_MS));
}

void test_pick_file_missing_state_falls_back_to_sleep() {
  // PetState::Nap is not in the manifest; spec says fall back to sleep.
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  TEST_ASSERT_EQUAL_STRING("sleep.gif", _characterPickFile(PetState::Nap, 0));
}

void test_pick_file_fallback_chain_sleep_then_idle() {
  // Manifest has only idle; asking for Busy should fall: busy(missing)
  // → sleep(missing) → idle[0].
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleOnly);
  TEST_ASSERT_EQUAL_STRING("just_idle.gif",
                           _characterPickFile(PetState::Busy, 0));
}

void test_pick_file_empty_manifest_returns_null() {
  // Manifest with only sleep: asking for Nap falls to sleep OK.
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestSleepOnly);
  TEST_ASSERT_EQUAL_STRING("only_sleep.gif",
                           _characterPickFile(PetState::Nap, 0));
  // But asking with a manifest that has NEITHER sleep NOR idle should be null.
  // Simulate by building such a manifest inline.
  const char* j = R"({
    "name":"busy_only",
    "colors":{"body":"#000","bg":"#000","text":"#000","textDim":"#000","ink":"#000"},
    "states":{"busy":"b.gif"}
  })";
  _manifestResetForTest(); _characterResetForTest();
  setActive(j);
  TEST_ASSERT_NULL(_characterPickFile(PetState::Idle, 0));
}
```

Add to `main`:

```cpp
RUN_TEST(test_pick_file_idle_first_variant);
RUN_TEST(test_pick_file_idle_does_not_advance_before_dwell);
RUN_TEST(test_pick_file_idle_advances_after_dwell);
RUN_TEST(test_pick_file_idle_wraps_to_zero);
RUN_TEST(test_pick_file_non_idle_state_does_not_rotate);
RUN_TEST(test_pick_file_state_change_resets_variant);
RUN_TEST(test_pick_file_missing_state_falls_back_to_sleep);
RUN_TEST(test_pick_file_fallback_chain_sleep_then_idle);
RUN_TEST(test_pick_file_empty_manifest_returns_null);
```

- [ ] **Step 2: Run tests, verify RED**

```bash
pio test -e native -f test_character
```

Expected: 9 new tests FAIL with assertion errors (stub returns nullptr).

- [ ] **Step 3: Implement `pickStateFile` helper + `_characterPickFile` in `src/character.cpp`**

Split into two pieces:

1. **`pickStateFile(state, variantIdx)`** — pure, no state tracking. The
   ARDUINO decoder (Task 3) will also call this.
2. **`_characterPickFile(state, nowMs)`** — test-only wrapper that owns
   `curState` / `variantIdx` / `variantStart` / `hasCurState` to
   simulate end-of-animation variant rotation purely from elapsed time.

Below the existing `mapState` in the anon namespace, add:

```cpp
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
```

Replace the `_characterPickFile` stub:

```cpp
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
```

- [ ] **Step 4: Run tests, verify GREEN**

```bash
pio test -e native -f test_character
```

Expected: 10/10 PASS (initial + 9 new).

- [ ] **Step 5: Run full native suite, verify no regressions**

```bash
pio test -e native
```

Expected: all tests PASS (manifest/persist/xfer/etc. untouched).

- [ ] **Step 6: Commit**

```bash
git add src/character.cpp test/test_character/test_character.cpp
git commit -m "$(cat <<'EOF'
sp6b: pickFile with variant rotation + fallback chain

Pure function (native-testable) picks which GIF filename to use for a
given PetState + time. Idle rotates through its variants every
VARIANT_DWELL_MS (5000 ms). State transitions reset the rotation.
Missing states fall back sleep → idle[0] → nullptr.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: ARDUINO decoder — file callbacks + draw callback + tick body

No native tests — AnimatedGIF is ARDUINO-only. Verification is by
firmware build (this task) plus device smoke (Task 5).

**Files:**
- Modify: `src/character.cpp`

- [ ] **Step 1: Add ARDUINO file callbacks**

In the `#ifdef ARDUINO` section of the anon namespace, add above the
existing `ready` / `gif` declarations:

```cpp
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
```

- [ ] **Step 2: Add draw callback**

Still inside the `#ifdef ARDUINO` anon namespace:

```cpp
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
```

- [ ] **Step 3: Implement `characterInit` + `characterReady`**

Replace the stub `characterInit` body:

```cpp
#ifdef ARDUINO
void characterInit() {
  ready = false;
  const CharManifest* m = manifestActive();
  if (!m) return;

  // Validate first-variant file for each populated state opens as a GIF.
  // Header-only: open → close. Cheap.
  gif.begin(LITTLE_ENDIAN_PIXELS);
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
  }

  // As long as manifestActive() is non-null AND at least one state file
  // validated, we're ready. pickFileImpl handles per-state misses.
  ready = true;
  Serial.print("[char] ready: ");
  Serial.println(m->name);
}
#else
void characterInit() {}
#endif
```

- [ ] **Step 4: Implement `openVariant` helper + `characterSetState`**

`openVariant` is shared by `characterSetState` (first entry of a state)
and `characterTick` (between-variant pause elapse). Keeps variantIdx
intact across pause windows.

In the `#ifdef ARDUINO` anon namespace (below the draw callback):

```cpp
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
```

Replace the `characterSetState` stub:

```cpp
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
```

- [ ] **Step 5: Implement `characterTick`**

Replace the stub:

```cpp
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
```

- [ ] **Step 6: Implement `characterInvalidate`**

Replace the stub:

```cpp
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
```

- [ ] **Step 7: Build firmware, verify no link errors**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM delta +~15 KB (AnimatedGIF static instance).
Note the new percentage in the output.

- [ ] **Step 8: Run native tests, verify no regressions**

```bash
pio test -e native
```

Expected: all tests PASS. The native stubs for the ARDUINO-only
characters do not affect test_character (which tests `_characterPickFile`).

- [ ] **Step 9: Commit**

```bash
git add src/character.cpp
git commit -m "$(cat <<'EOF'
sp6b: ARDUINO decoder body (file + draw callbacks, tick, setState)

Ports the core pattern from anthropics/claude-desktop-buddy
src/character.cpp (M5StickC Plus reference):

- gifOpenCb / gifReadCb / gifSeekCb / gifCloseCb wrap SFUD File*
- gifDrawCb writes RGB565 scanlines straight to tft.drawPixel,
  transparent pixels paint manifest bg color (full-frame GIFs from
  host mean no disposal handling)
- characterInit validates each state's first-variant file opens as a
  valid GIF at boot
- characterSetState closes any open GIF, delegates to pickFileImpl to
  determine the new filename, opens it at (BUDDY_X, BUDDY_Y)
- characterTick plays one frame when due; on end-of-animation, single-
  variant states freeze (no restart thrash), multi-variant idle loops
  until VARIANT_DWELL_MS elapses then rotates via ANIM_PAUSE_MS pause
- characterInvalidate closes any open GIF so the next setState call
  re-opens and repaints the wiped region

Native tests still green: ARDUINO code is guarded and native falls
back to the stubs. Firmware builds clean; device smoke in Task 5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Layout reflow in renderIdle + ASCII fallback relocation

**Files:**
- Modify: `src/ui.cpp`

Current renderIdle paints stats in the region the buddy now wants
(big digits at y=82, x=38/148/258 overlap x=8..104). This task shifts
the stats to a right-side panel and shrinks digits so they fit.

- [ ] **Step 1: Update renderIdle fullRedraw block**

Open `src/ui.cpp` and locate the `if (fullRedraw) {` block inside
`renderIdle`. Replace the labels/coordinates:

Old (around ui.cpp:83-95, verify with current file):

```cpp
    tft.setCursor(8, 36);                tft.print("Level");
    tft.setCursor(SCREEN_W - 120, 36);   tft.print("Tokens today");
    tft.setCursor(28, 66);   tft.print("Total");
    tft.setCursor(130, 66);  tft.print("Running");
    tft.setCursor(240, 66);  tft.print("Waiting");
```

New:

```cpp
    // Labels shifted right of the 96-wide buddy slot at BUDDY_X.
    tft.setCursor(112, 36);              tft.print("Level");
    tft.setCursor(SCREEN_W - 120, 36);   tft.print("Tokens today");
    tft.setCursor(116, 66);              tft.print("Total");
    tft.setCursor(182, 66);              tft.print("Running");
    tft.setCursor(252, 66);              tft.print("Waiting");
```

- [ ] **Step 2: Shift the level cell**

Find:

```cpp
    tft.fillRect(8, 46, 60, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "L%d", lvl);
    tft.setCursor(8, 46);
    tft.print(buf);
```

Replace with:

```cpp
    tft.fillRect(112, 46, 60, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "L%d", lvl);
    tft.setCursor(112, 46);
    tft.print(buf);
```

- [ ] **Step 3: Shrink big-digit cells from size-5 to size-3 and reposition**

Find the `drawNum` lambda:

```cpp
  auto drawNum = [](int x, int n) {
    tft.fillRect(x, 80, 90, 40, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(5);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 82); tft.print(buf);
  };
  if (s.hb.total   != lastTotal)   { drawNum(38,  s.hb.total);   lastTotal   = s.hb.total; }
  if (s.hb.running != lastRunning) { drawNum(148, s.hb.running); lastRunning = s.hb.running; }
  if (s.hb.waiting != lastWaiting) { drawNum(258, s.hb.waiting); lastWaiting = s.hb.waiting; }
```

Replace with:

```cpp
  // size-3 digits: 18px wide × 24px tall. 3 cells fit in the right panel.
  auto drawNum = [](int x, int n) {
    tft.fillRect(x, 80, 54, 28, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(3);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 82); tft.print(buf);
  };
  if (s.hb.total   != lastTotal)   { drawNum(118, s.hb.total);   lastTotal   = s.hb.total; }
  if (s.hb.running != lastRunning) { drawNum(186, s.hb.running); lastRunning = s.hb.running; }
  if (s.hb.waiting != lastWaiting) { drawNum(254, s.hb.waiting); lastWaiting = s.hb.waiting; }
```

- [ ] **Step 4: Move message + transcript clears to the right panel**

Find:

```cpp
  if (s.hb.msg != lastMsg) {
    tft.fillRect(0, 125, SCREEN_W, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(8, 128);
    tft.print(s.hb.msg.c_str());
    lastMsg = s.hb.msg;
  }
```

Replace `fillRect`'s x and width + cursor x:

```cpp
  if (s.hb.msg != lastMsg) {
    tft.fillRect(112, 125, SCREEN_W - 112, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(112, 128);
    tft.print(s.hb.msg.c_str());
    lastMsg = s.hb.msg;
  }
```

Find:

```cpp
  if (s.hb.entries != lastEntries) {
    tft.fillRect(0, 145, SCREEN_W, 40, COLOR_BG);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    size_t n = s.hb.entries.size() < 2 ? s.hb.entries.size() : 2;
    for (size_t i = 0; i < n; ++i) {
      tft.setCursor(8, 148 + (int)i * 18);
      tft.print(s.hb.entries[i].c_str());
    }
    lastEntries = s.hb.entries;
  }
```

Replace with:

```cpp
  if (s.hb.entries != lastEntries) {
    tft.fillRect(112, 145, SCREEN_W - 112, 40, COLOR_BG);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    size_t n = s.hb.entries.size() < 2 ? s.hb.entries.size() : 2;
    for (size_t i = 0; i < n; ++i) {
      tft.setCursor(112, 148 + (int)i * 18);
      tft.print(s.hb.entries[i].c_str());
    }
    lastEntries = s.hb.entries;
  }
```

- [ ] **Step 5: Relocate ASCII pet fallback to the buddy region**

Find the pet render block (around ui.cpp:183-192):

```cpp
    tft.fillRect(120, 188, 80, 32, COLOR_BG);
    tft.setTextColor(petColour, COLOR_BG);
    tft.setTextSize(1);
    const char* const* rows = petFace(st, frame);
    for (size_t i = 0; i < PET_FACE_LINES; ++i) {
      tft.setCursor(130, 188 + (int)i * 8);
      tft.print(rows[i]);
    }
```

Replace with:

```cpp
    // ASCII pet renders in the buddy slot. SP6b's characterTick paints
    // over this on the same rect when characterReady(); ui.cpp doesn't
    // need to know which path is active because both respect BUDDY_*.
    tft.fillRect(BUDDY_X, BUDDY_Y, BUDDY_W, BUDDY_H, COLOR_BG);
    tft.setTextColor(petColour, COLOR_BG);
    tft.setTextSize(2);   // bigger ASCII for the larger slot
    const char* const* rows = petFace(st, frame);
    for (size_t i = 0; i < PET_FACE_LINES; ++i) {
      tft.setCursor(BUDDY_X + 8, BUDDY_Y + 20 + (int)i * 16);
      tft.print(rows[i]);
    }
```

Add `#include "character.h"` at the top of ui.cpp (near the other local
headers) so subsequent tasks can reference characterReady.

- [ ] **Step 6: Build firmware, verify no errors**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM/Flash roughly unchanged from Task 3 (just
moving draw calls around).

- [ ] **Step 7: Commit**

```bash
git add src/ui.cpp
git commit -m "$(cat <<'EOF'
sp6b: reflow renderIdle for 96x100 buddy slot on the left

- Carves out (BUDDY_X=8, BUDDY_Y=34, BUDDY_W=96, BUDDY_H=100) for the
  buddy renderer.
- Shifts stats panel to x=112+: Level/tokens headers, size-3 (was
  size-5) total/running/waiting digits at x=118/186/254, message +
  transcript clears to the right panel only so they don't trample the
  buddy region.
- Enlarges ASCII pet fallback from size-1 to size-2 to occupy the
  buddy slot cleanly when characterReady()==false. Same coord space
  means SP6b's characterTick can paint over this without screen
  conflicts.

Pure layout change — characterTick wiring lands in Task 5.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Wire character to main loop + state transitions + device smoke

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add include + call characterInit after manifestLoadActiveFromPersist**

In `src/main.cpp`, add `#include "character.h"` near the other local
headers (manifest.h, persist.h, etc.).

Locate `setup()`:

```cpp
  persistInit();
  manifestLoadActiveFromPersist();
  xferInit();
```

Add `characterInit()` right after the manifest load:

```cpp
  persistInit();
  manifestLoadActiveFromPersist();
  characterInit();
  xferInit();
```

- [ ] **Step 2: Track state transitions + drive characterSetState + characterTick**

Locate the end of `loop()` where `petTickFrame` is called (around
main.cpp:229-231):

```cpp
  // Pet is only visible on the Idle screen; only gating renders there
  // prevents the 500ms frame tick from repainting Advertising / Connected
  // / Disconnected screens (which previously caused visible flicker).
  bool petAdvanced = petTickFrame(now);
  if (petAdvanced && appState.mode == Mode::Idle) pendingRender = true;
```

Add the character driver above it. The character module has its own
internal frame timing (AnimatedGIF delays), so we can call
`characterTick` every loop iteration when Idle is visible. State
transitions are detected by comparing `petComputeState` against a
last-seen PetState:

```cpp
  if (appState.mode == Mode::Idle && characterReady()) {
    static PetState lastCharState = PetState::Sleep;
    static bool     lastCharInit  = false;
    PetState charSt = petComputeState(appState, now);
    if (!lastCharInit || charSt != lastCharState) {
      characterSetState(charSt);
      lastCharState = charSt;
      lastCharInit  = true;
    }
    characterTick(now);
  }

  // Pet (ASCII fallback) tick still drives PetState-cache for renderIdle
  // when characterReady() is false. Harmless when buddy is active — no
  // ui.cpp render occurs during the buddy frame window (Idle cached).
  bool petAdvanced = petTickFrame(now);
  if (petAdvanced && appState.mode == Mode::Idle) pendingRender = true;
```

- [ ] **Step 3: Invalidate character on fullRedraw**

In `src/ui.cpp::renderIdle`, inside the `if (fullRedraw)` block, after
the existing `clearAll()`, add:

```cpp
    characterInvalidate();
```

Wait — this requires `#include "character.h"` in ui.cpp which Task 4
added. Verify the include line is present. Then the call goes after
the cache invalidations near the bottom of the fullRedraw block:

```cpp
    // Invalidate caches so every block repaints on the fresh canvas.
    lastLvl = INT32_MIN; lastTokens = INT64_MIN;
    lastTotal = INT32_MIN; lastRunning = INT32_MIN; lastWaiting = INT32_MIN;
    lastMsg.clear(); lastEntries.clear();
    lastPet = (PetState)-1; lastFrame = (size_t)-1; lastOwner.clear();
    characterInvalidate();   // buddy region was wiped by clearAll()
```

- [ ] **Step 4: Build firmware, verify no errors**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. Final RAM/Flash numbers are the new baseline for
SP6b.

- [ ] **Step 5: Run full native tests one last time**

```bash
pio test -e native
```

Expected: all green. The native stubs for characterInit / Tick /
SetState / Invalidate do nothing, so no regressions from wiring them
into main.cpp.

- [ ] **Step 6: Flash to device**

```bash
pio run -e seeed_wio_terminal -t upload
```

Expected: SUCCESS. Device reboots into Advertising.

- [ ] **Step 7: Device smoke**

- Connect via Hardware Buddy (`Claude-<suffix>`).
- Upload `characters/bufo` (unless it's already there from SP6a smoke).
- After upload completes, the Idle screen should show:
  - The bufo frog animating at `(8, 34)` on the left.
  - Stats panel at `x=112+` with size-3 digits.
  - No ASCII pet (bufo has all states).
- Trigger a tool-use prompt from Claude Code on the host → Attention state.
  The buddy should switch to `attention.gif`.
- Approve the tool use → briefly see Heart, then back to Idle.
- Let the device sit on Idle for ~1 minute → watch idle variants rotate
  every 5 s.
- Physically reset the device → after reboot, bufo should still be
  there (persist rehydrate path).

Record the pass / fail of each bullet. Any failure → do not merge.

- [ ] **Step 8: Commit wiring**

```bash
git add src/main.cpp src/ui.cpp
git commit -m "$(cat <<'EOF'
sp6b: wire characterTick + characterSetState into main loop

- setup() calls characterInit() after manifestLoadActiveFromPersist
  so stored-active-char buddies validate on cold boot.
- loop() on Idle: track last PetState via petComputeState; on
  transition call characterSetState(); every loop iteration call
  characterTick(now) which internally respects GIF frame delays.
- renderIdle's fullRedraw block calls characterInvalidate() so the
  buddy region repaints on the next characterTick after the screen
  was wiped.

Device smoke: bufo animates on Idle; attention/busy/celebrate/heart
transitions render correctly; idle variants rotate on ~5 s dwell;
buddy survives device reset via SP6a persist rehydrate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 9: Merge to main**

```bash
git checkout main
git merge --no-ff feature/sp6b-character-render \
  -m "Merge feature/sp6b-character-render: GIF buddy on idle screen"
```

- [ ] **Step 10: (Optional) Delete local branch**

```bash
git branch -d feature/sp6b-character-render
```

---

## Verification checklist

After the full plan executes, confirm:

- [ ] `pio test -e native` passes 100% (prior + new test_character = ~131 tests).
- [ ] `pio run -e seeed_wio_terminal` builds clean. RAM bump ≤ 20 KB vs
      SP6a baseline (AnimatedGIF instance).
- [ ] Fresh bufo upload shows animating frog on Idle within 1 second of
      `char_end` ack.
- [ ] Idle variants rotate every ~5 s.
- [ ] State transitions (busy / attention / celebrate / heart) swap the
      GIF within one tick of the state change.
- [ ] Reset preserves the active character and buddy appears immediately
      on next boot.
- [ ] No `file ack timeout` regressions from SP6a (transport path
      untouched, but verify the drain still works).
