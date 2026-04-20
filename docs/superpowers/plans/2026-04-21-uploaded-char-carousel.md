# Uploaded Char Carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let user cycle through uploaded chars in `/chars/` using the 5-way joystick's left/right, with a 1.5s name overlay after each switch.

**Architecture:** New `src/carousel.cpp` module (native-testable enumerate + advance logic, ARDUINO-only SFUD backend). Two new button events (`PressLeft`/`PressRight`). Two AppState fields for the overlay (`buddyOverlayUntilMs`, `buddyOverlayName`). One new character API (`characterRefreshManifest`) to swap GIF after the active manifest changes. Main loop dispatches Left/Right only in `Mode::Idle` and detects overlay expiry to trigger a clean repaint.

**Tech Stack:** Arduino/PlatformIO (atmelsam SAMD51), Seeed_Arduino_FS+SFUD (QSPI flash), AnimatedGIF, TFT_eSPI, Unity (native tests via ArduinoJson, no Arduino hardware).

**Spec reference:** `docs/superpowers/specs/2026-04-21-uploaded-char-carousel-design.md`

---

## Task 1: Branch + carousel skeleton + native test registration

Create a clean branch off `main`, add the carousel source files as stubs, register an empty test suite, and confirm both Arduino build and native test run are green before writing any real logic.

**Files:**
- Create: `src/carousel.h`
- Create: `src/carousel.cpp`
- Create: `test/test_carousel/test_carousel.cpp`
- Modify: `platformio.ini` (native `build_src_filter`)

- [ ] **Step 1: Create feature branch**

```bash
git checkout main
git pull --ff-only
git checkout -b feature/char-carousel
```

- [ ] **Step 2: Create `src/carousel.h`**

```cpp
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
```

- [ ] **Step 3: Create `src/carousel.cpp` with stubs**

```cpp
#include "carousel.h"
#include "state.h"
#include "persist.h"
#include "character.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
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
```

- [ ] **Step 4: Create `test/test_carousel/test_carousel.cpp` with empty suite**

```cpp
#include <unity.h>
#include "carousel.h"

int main(int, char**) {
  UNITY_BEGIN();
  return UNITY_END();
}
```

- [ ] **Step 5: Add `carousel.cpp` to native `build_src_filter`**

In `platformio.ini`, change the `[env:native]` section's `build_src_filter` line from:

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet.cpp> +<xfer.cpp> +<manifest.cpp> +<character.cpp>
```

to:

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet.cpp> +<xfer.cpp> +<manifest.cpp> +<character.cpp> +<carousel.cpp>
```

- [ ] **Step 6: Verify Arduino build is green**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS, no warnings about undefined references.

- [ ] **Step 7: Verify native tests are green**

Run: `pio test -e native`
Expected: all existing suites pass; the new `test_carousel` suite runs with `0 Tests, 0 Failures`.

- [ ] **Step 8: Commit**

```bash
git add src/carousel.h src/carousel.cpp test/test_carousel/test_carousel.cpp platformio.ini
git commit -m "carousel: skeleton + native test wiring

No behaviour yet. Task 2 fills in carouselEnumerate."
```

---

## Task 2: carouselEnumerate pure logic (native TDD)

Implement alphabetical sort and truncation in `carouselEnumerate` using the native fake-char injection hook. The ARDUINO-side SFUD enumeration comes later in Task 8 — this task only exercises the pure algorithm.

**Files:**
- Modify: `src/carousel.cpp` (native branch only)
- Modify: `test/test_carousel/test_carousel.cpp`

- [ ] **Step 1: Write failing test for empty list**

Open `test/test_carousel/test_carousel.cpp` and replace the whole file with:

```cpp
#include <unity.h>
#include <cstring>
#include "carousel.h"

void test_enumerate_empty_list() {
  _carouselSetFakeChars(nullptr, 0);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_enumerate_empty_list);
  return UNITY_END();
}
```

- [ ] **Step 2: Run tests — verify failure (if any)**

Run: `pio test -e native -f test_carousel`
Expected: test_enumerate_empty_list PASSES (returning 0 is already the stub). Continue — this test is a baseline.

- [ ] **Step 3: Add failing test for sorted 3-char list**

After `test_enumerate_empty_list`, before `main`, add:

```cpp
void test_enumerate_sorts_alphabetically() {
  const char* input[] = {"zebra", "apple", "mango"};
  _carouselSetFakeChars(input, 3);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(3, n);
  TEST_ASSERT_EQUAL_STRING("apple", out[0]);
  TEST_ASSERT_EQUAL_STRING("mango", out[1]);
  TEST_ASSERT_EQUAL_STRING("zebra", out[2]);
}
```

And register it in `main` before `return UNITY_END()`:
```cpp
RUN_TEST(test_enumerate_sorts_alphabetically);
```

- [ ] **Step 4: Run — verify failure**

Run: `pio test -e native -f test_carousel`
Expected: FAIL — `test_enumerate_sorts_alphabetically` expects count 3 but gets 0.

- [ ] **Step 5: Implement `carouselEnumerate` (native branch)**

In `src/carousel.cpp`, replace the stub body of `carouselEnumerate` with:

```cpp
size_t carouselEnumerate(CarouselName* out, size_t max) {
#ifndef ARDUINO
  size_t n = fakeCount < max ? fakeCount : max;
  for (size_t i = 0; i < n; ++i) {
    std::strncpy(out[i], fakeChars[i], MANIFEST_NAME_MAX);
    out[i][MANIFEST_NAME_MAX] = '\0';
  }
  // Simple insertion sort: n <= 16, allocations avoided.
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
#else
  (void)out; (void)max;
  return 0;   // Task 8 fills in the SFUD-backed version.
#endif
}
```

- [ ] **Step 6: Run — verify tests pass**

Run: `pio test -e native -f test_carousel`
Expected: both tests PASS.

- [ ] **Step 7: Add test for truncation**

Add this test before `main`:

```cpp
void test_enumerate_truncates_at_max() {
  const char* input[20] = {
    "a01","a02","a03","a04","a05","a06","a07","a08",
    "a09","a10","a11","a12","a13","a14","a15","a16",
    "a17","a18","a19","a20",
  };
  _carouselSetFakeChars(input, 20);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(CAROUSEL_MAX_CHARS, n);
  // First 16 in sorted order should survive; a17..a20 dropped.
  TEST_ASSERT_EQUAL_STRING("a01", out[0]);
  TEST_ASSERT_EQUAL_STRING("a16", out[15]);
}
```

And in `main`:
```cpp
RUN_TEST(test_enumerate_truncates_at_max);
```

- [ ] **Step 8: Run — verify all tests pass**

Run: `pio test -e native -f test_carousel`
Expected: 3/3 PASS.

Note: truncation is based on `_carouselSetFakeChars` capping at `CAROUSEL_MAX_CHARS`, which is already implemented. The test confirms behaviour end-to-end.

- [ ] **Step 9: Commit**

```bash
git add src/carousel.cpp test/test_carousel/test_carousel.cpp
git commit -m "carousel: carouselEnumerate native impl with sort + cap

- Inserts via native fake-char hook.
- Alphabetical sort via insertion sort (n<=16).
- Cap at CAROUSEL_MAX_CHARS."
```

---

## Task 3: carouselAdvance TDD (0/1/N chars, wrap, missing-current)

Implement the carousel advance logic driven entirely by persist + fake chars. Skips the real manifest/character swaps on native; those are no-ops there.

**Files:**
- Modify: `src/carousel.cpp`
- Modify: `src/state.h` (add overlay fields — needed by test harness to observe result)
- Modify: `test/test_carousel/test_carousel.cpp`
- Modify: `src/config.h` (BUDDY_OVERLAY_MS)

- [ ] **Step 1: Add AppState overlay fields**

In `src/state.h`, inside the `struct AppState { ... }` block, after `uint32_t ackUntilMs = 0;`, add:

```cpp
  uint32_t buddyOverlayUntilMs = 0;
  char     buddyOverlayName[33] = {0};
```

- [ ] **Step 2: Add BUDDY_OVERLAY_MS constant**

In `src/config.h`, after the existing `VARIANT_DWELL_MS` / `ANIM_PAUSE_MS` block at the end, add:

```cpp
// --- Carousel ---
static constexpr uint32_t BUDDY_OVERLAY_MS = 1500;
```

- [ ] **Step 3: Write failing test for 0-char no-op**

In `test/test_carousel/test_carousel.cpp`, after the enumerate tests, add:

```cpp
#include "state.h"
#include "persist.h"

static void resetCarouselState() {
  _persistResetFakeFile();
  persistInit();
  _carouselSetFakeChars(nullptr, 0);
}

void test_advance_zero_chars_is_noop() {
  resetCarouselState();
  AppState s;
  s.buddyOverlayUntilMs = 0;
  bool ok = carouselAdvance(s, true, 1000);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0, s.buddyOverlayUntilMs);
  TEST_ASSERT_EQUAL_STRING("", s.buddyOverlayName);
}
```

Register in `main`:
```cpp
RUN_TEST(test_advance_zero_chars_is_noop);
```

- [ ] **Step 4: Run — verify failure**

Run: `pio test -e native -f test_carousel`
Expected: PASS (returning false is already the stub). Continue; this is a baseline.

- [ ] **Step 5: Add failing test for single-char overlay-only**

Add:

```cpp
void test_advance_one_char_sets_overlay_only() {
  resetCarouselState();
  persistSetActiveChar("bufo");
  const char* input[] = {"bufo"};
  _carouselSetFakeChars(input, 1);

  AppState s;
  bool ok = carouselAdvance(s, true, 5000);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(5000 + 1500, s.buddyOverlayUntilMs);
  TEST_ASSERT_EQUAL_STRING("bufo", s.buddyOverlayName);
  // persist unchanged (still "bufo").
  TEST_ASSERT_EQUAL_STRING("bufo", persistGetActiveChar());
}
```

Register:
```cpp
RUN_TEST(test_advance_one_char_sets_overlay_only);
```

- [ ] **Step 6: Run — verify failure**

Run: `pio test -e native -f test_carousel`
Expected: FAIL — stub returns false.

- [ ] **Step 7: Add failing tests for multi-char advance**

Add:

```cpp
void test_advance_forward_wraps() {
  resetCarouselState();
  persistSetActiveChar("alpha");
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  // alpha → beta
  carouselAdvance(s, true, 100);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
  TEST_ASSERT_EQUAL_STRING("beta", s.buddyOverlayName);

  // beta → gamma
  carouselAdvance(s, true, 200);
  TEST_ASSERT_EQUAL_STRING("gamma", persistGetActiveChar());

  // gamma → alpha (wrap)
  carouselAdvance(s, true, 300);
  TEST_ASSERT_EQUAL_STRING("alpha", persistGetActiveChar());
}

void test_advance_backward_wraps() {
  resetCarouselState();
  persistSetActiveChar("alpha");
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  // alpha → gamma (wrap backward)
  carouselAdvance(s, false, 100);
  TEST_ASSERT_EQUAL_STRING("gamma", persistGetActiveChar());

  // gamma → beta
  carouselAdvance(s, false, 200);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
}

void test_advance_when_active_not_in_list() {
  resetCarouselState();
  persistSetActiveChar("deleted-char");  // no longer exists
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  // Missing → treat as idx 0 (alpha); forward → beta.
  carouselAdvance(s, true, 100);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
}
```

Register all three:
```cpp
RUN_TEST(test_advance_forward_wraps);
RUN_TEST(test_advance_backward_wraps);
RUN_TEST(test_advance_when_active_not_in_list);
```

- [ ] **Step 8: Run — verify failures**

Run: `pio test -e native -f test_carousel`
Expected: new tests FAIL (stub returns false, persist unchanged).

- [ ] **Step 9: Implement `carouselAdvance`**

In `src/carousel.cpp`, replace the stub body of `carouselAdvance` with:

```cpp
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

  std::strncpy(s.buddyOverlayName, names[newIdx], 32);
  s.buddyOverlayName[32] = '\0';
  s.buddyOverlayUntilMs = nowMs + BUDDY_OVERLAY_MS;
  return true;
}
```

Note: `characterRefreshManifest` does not exist yet — Task 5 adds it. To make this task compile standalone, add a temporary forward declaration in `src/carousel.cpp` above the functions, OR skip the call for this task and reintroduce it in Task 5. **Go with the forward decl approach**: add after the `#include` block in `src/carousel.cpp`:

```cpp
// Forward-declared here so Task 3 can land before Task 5's character.h edit.
// Task 5 promotes this into character.h and removes this extern.
extern void characterRefreshManifest();
```

Then in `src/character.cpp`, add a temporary no-op body so the link succeeds for both native and Arduino builds. Near the end of the file, before the `#ifndef ARDUINO` test-only block, add:

```cpp
void characterRefreshManifest() {}
```

Task 5 will flesh this out.

- [ ] **Step 10: Run — verify all tests pass**

Run: `pio test -e native -f test_carousel`
Expected: 7/7 PASS.

- [ ] **Step 11: Verify Arduino build still green**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS.

- [ ] **Step 12: Commit**

```bash
git add src/carousel.cpp src/character.cpp src/state.h src/config.h \
        test/test_carousel/test_carousel.cpp
git commit -m "carousel: advance logic w/ persist + overlay (TDD)

- carouselAdvance handles 0/1/N chars with wrap.
- AppState gains buddyOverlayUntilMs + buddyOverlayName.
- characterRefreshManifest stub added; real impl in later task."
```

---

## Task 4: Button events PressLeft / PressRight

Wire 5-way joystick left/right into the existing button debounce layer. No tests (hardware-only).

**Files:**
- Modify: `src/buttons.h`
- Modify: `src/buttons.cpp`

- [ ] **Step 1: Add enum values**

In `src/buttons.h`, replace:

```cpp
enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,       // 5-way center: instant press edge
  LongPressNav,   // 5-way center: emitted once after hold exceeds BUTTON_LONG_PRESS_MS
};
```

with:

```cpp
enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,       // 5-way center: instant press edge
  LongPressNav,   // 5-way center: emitted once after hold exceeds BUTTON_LONG_PRESS_MS
  PressLeft,      // 5-way left edge
  PressRight,     // 5-way right edge
};
```

- [ ] **Step 2: Add entries to btns[] table**

In `src/buttons.cpp`, replace:

```cpp
static Btn btns[] = {
  {WIO_KEY_A,    true, true, 0, ButtonEvent::PressA},
  {WIO_KEY_B,    true, true, 0, ButtonEvent::PressB},
  {WIO_KEY_C,    true, true, 0, ButtonEvent::PressC},
  {WIO_5S_PRESS, true, true, 0, ButtonEvent::PressNav},
};
```

with:

```cpp
static Btn btns[] = {
  {WIO_KEY_A,    true, true, 0, ButtonEvent::PressA},
  {WIO_KEY_B,    true, true, 0, ButtonEvent::PressB},
  {WIO_KEY_C,    true, true, 0, ButtonEvent::PressC},
  {WIO_5S_PRESS, true, true, 0, ButtonEvent::PressNav},
  {WIO_5S_LEFT,  true, true, 0, ButtonEvent::PressLeft},
  {WIO_5S_RIGHT, true, true, 0, ButtonEvent::PressRight},
};
```

- [ ] **Step 3: Verify Arduino build**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS. `WIO_5S_LEFT` and `WIO_5S_RIGHT` are defined by the Seeed board variant.

- [ ] **Step 4: Verify native tests still pass**

Run: `pio test -e native`
Expected: all suites PASS (buttons.cpp is Arduino-only — native tests don't link against it).

- [ ] **Step 5: Commit**

```bash
git add src/buttons.h src/buttons.cpp
git commit -m "buttons: add PressLeft / PressRight for 5-way joystick"
```

---

## Task 5: characterRefreshManifest real implementation

Replace the no-op stub from Task 3 with the real logic that closes any open GIF, resets internal state, and re-validates against the current `manifestActive()` so the next `characterSetState` call reopens with the new manifest's files.

**Files:**
- Modify: `src/character.h`
- Modify: `src/character.cpp`
- Modify: `src/carousel.cpp` (remove the forward decl)

- [ ] **Step 1: Declare in character.h**

In `src/character.h`, after the existing `void characterInvalidate();` declaration, add:

```cpp
// Called after the active manifest has been swapped via manifestSetActive.
// Closes any open GIF, resets the internal state-tracking so the next
// characterSetState() call opens a fresh file from the new manifest, and
// re-runs characterInit-style validation to update characterReady().
// On native, a no-op.
void characterRefreshManifest();
```

- [ ] **Step 2: Replace stub impl in character.cpp**

In `src/character.cpp`, delete the temporary no-op:

```cpp
void characterRefreshManifest() {}
```

that Task 3 added before the `#ifndef ARDUINO` block, and replace it with:

```cpp
#ifdef ARDUINO
void characterRefreshManifest() {
  // 1. Close any open decoder.
  if (gifOpen) { gif.close(); gifOpen = false; }
  animPauseUntil = 0;
  nextFrameAt    = 0;

  // 2. Reset state-tracking so caller's subsequent characterSetState()
  //    (even to the same PetState) re-opens a file.
  hasCurState = false;
  variantIdx  = 0;

  // 3. Re-validate readiness against the *new* manifest using the same
  //    cheap header-only open/close loop as characterInit.
  ready = false;
  const CharManifest* m = manifestActive();
  if (!m) return;

  bool anyOk = false;
  for (int i = 0; i < MANIFEST_STATE_COUNT; ++i) {
    if (m->stateVariantCount[i] == 0) continue;
    char path[96];
    std::snprintf(path, sizeof(path), "/chars/%s/%s",
                  m->name, m->states[i][0]);
    if (!gif.open(path, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) {
      continue;
    }
    gif.close();
    anyOk = true;
  }
  ready = anyOk;
}
#else
void characterRefreshManifest() {}
#endif
```

- [ ] **Step 3: Remove forward declaration from carousel.cpp**

In `src/carousel.cpp`, delete these lines added in Task 3:

```cpp
// Forward-declared here so Task 3 can land before Task 5's character.h edit.
// Task 5 promotes this into character.h and removes this extern.
extern void characterRefreshManifest();
```

The declaration is now in `character.h`, which `carousel.cpp` already includes.

- [ ] **Step 4: Verify Arduino build**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS.

- [ ] **Step 5: Verify native tests**

Run: `pio test -e native`
Expected: all suites PASS (native still gets empty stub).

- [ ] **Step 6: Commit**

```bash
git add src/character.h src/character.cpp src/carousel.cpp
git commit -m "character: characterRefreshManifest real impl

Closes open GIF, resets state-tracking + variant counter, re-validates
readiness against manifestActive(). Native remains a no-op stub."
```

---

## Task 6: renderIdle overlay strip paint

Paint a 1-line background-filled strip at the top of the buddy region showing `s.buddyOverlayName` whenever `now < s.buddyOverlayUntilMs`. The strip overlaps the GIF's top band for its duration by design; on expiry, the main loop (Task 7) triggers a full repaint to clear it.

**Files:**
- Modify: `src/ui.cpp`

- [ ] **Step 1: Locate the overlay insertion point**

The overlay must paint AFTER the GIF's rect has been touched by the fullRedraw path. Looking at `renderIdle`, the natural spot is at the very end of the function, after the owner-name block (around `src/ui.cpp:~207`). The overlay sits on top of the buddy region without conflicting with the ASCII-pet fallback block (which only runs when `!characterReady()` — see lines 178-200; if ASCII pet is active the overlay covers it too, which is acceptable).

- [ ] **Step 2: Add overlay paint block**

In `src/ui.cpp`, at the end of `renderIdle` (just before the closing `}`), insert:

```cpp
  // Carousel name overlay: shows the char name for BUDDY_OVERLAY_MS after a
  // left/right press. Strip covers the top of the buddy region; main loop
  // triggers a full redraw on expiry to restore the underlying GIF.
  if (millis() < s.buddyOverlayUntilMs && s.buddyOverlayName[0] != '\0') {
    tft.fillRect(BUDDY_X, BUDDY_Y, BUDDY_W, 12, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(BUDDY_X + 2, BUDDY_Y + 2);
    tft.print(s.buddyOverlayName);
  }
```

- [ ] **Step 3: Verify Arduino build**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/ui.cpp
git commit -m "ui: paint carousel name overlay on idle screen"
```

---

## Task 7: main.cpp dispatch + overlay expiry

Dispatch `PressLeft` / `PressRight` from the existing button handler (Idle-only), and detect overlay expiry each loop iteration to trigger a clean repaint.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add carousel.h include**

In `src/main.cpp`, near the other local includes (after `#include "character.h"`), add:

```cpp
#include "carousel.h"
```

- [ ] **Step 2: Add button dispatch block**

In `src/main.cpp`, inside the `if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS) { ... }` block (starts around line 213), find the `else if (e == ButtonEvent::PressA || e == ButtonEvent::PressC)` branch (around line 230). AFTER this whole branch's closing `}`, BEFORE the closing `}` of the `if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS)` block, insert:

```cpp
    else if ((e == ButtonEvent::PressLeft || e == ButtonEvent::PressRight) &&
             appState.mode == Mode::Idle) {
      bool forward = (e == ButtonEvent::PressRight);
      if (carouselAdvance(appState, forward, now)) {
        lastButtonSendMs = now;
        render(true);
      }
    }
```

Note: `else if` chains onto the preceding `else if (e == ButtonEvent::PressA || e == ButtonEvent::PressC)`. Existing indentation / brace style matches the rest of the button handler.

- [ ] **Step 3: Add overlay-expiry detection**

Near the bottom of `loop()` in `src/main.cpp` (just before the final `delay(10);`), add:

```cpp
  // Overlay expiry: when the deadline first passes, force a full redraw
  // so the strip over the buddy region gets repainted with fresh GIF
  // content (the overlay paint block in renderIdle is self-gated on the
  // deadline, so this extra repaint cleans up the strip).
  static uint32_t lastOverlayDeadline = 0;
  if (appState.buddyOverlayUntilMs != lastOverlayDeadline) {
    lastOverlayDeadline = appState.buddyOverlayUntilMs;
  } else if (lastOverlayDeadline != 0 && now >= lastOverlayDeadline) {
    lastOverlayDeadline = 0;
    appState.buddyOverlayUntilMs = 0;
    if (appState.mode == Mode::Idle) {
      characterInvalidate();  // force GIF reopen after repaint
      pendingRender = true;
    }
  }
```

- [ ] **Step 4: Verify Arduino build**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "main: dispatch PressLeft/Right in Idle + handle overlay expiry

Left/Right only fires in Mode::Idle. On overlay expiry, invalidate
the character decoder and request full redraw so the strip clears."
```

---

## Task 8: ARDUINO SFUD enumeration

Implement the real `/chars/` directory listing for `carouselEnumerate` on the Arduino side. Uses Seeed_Arduino_FS's standard `openNextFile` iterator.

**Files:**
- Modify: `src/carousel.cpp`

- [ ] **Step 1: Replace the ARDUINO branch of carouselEnumerate**

In `src/carousel.cpp`, locate the `#else` / `return 0;` block inside `carouselEnumerate` (added in Task 2) and replace:

```cpp
#else
  (void)out; (void)max;
  return 0;   // Task 8 fills in the SFUD-backed version.
#endif
```

with:

```cpp
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
```

- [ ] **Step 2: Verify Arduino build**

Run: `pio run -e seeed_wio_terminal`
Expected: SUCCESS.

- [ ] **Step 3: Verify native tests still pass**

Run: `pio test -e native`
Expected: all 7 `test_carousel` tests PASS (native branch unchanged).

- [ ] **Step 4: Commit**

```bash
git add src/carousel.cpp
git commit -m "carousel: ARDUINO SFUD-backed /chars enumeration

Lists subdirectories under /chars, strips leading path, skips . / ..,
caps at CAROUSEL_MAX_CHARS, alphabetical sort."
```

---

## Task 9: Device smoke + merge

Flash the firmware, walk through every scenario from the spec's "User Experience" + "Edge cases" sections, then merge to `main`.

**Files:** none (device + git only).

- [ ] **Step 1: Flash firmware**

Put device in bootloader mode (double-click reset), then:

Run: `pio run -e seeed_wio_terminal -t upload`
Expected: "SUCCESS" on the upload, device reboots into firmware.

- [ ] **Step 2: Connect Claude Desktop and upload at least 2 chars**

From Claude Desktop's Hardware Buddy tab, push two or more uploaded chars so the device has multiple `/chars/<name>/manifest.json` entries on flash. If only one is available, upload a second manually for the multi-char tests below.

- [ ] **Step 3: Smoke test — multi-char forward**

On Idle screen (after a heartbeat arrives), push 5-way **right**. Expected:
- GIF swaps to a different char within ~300ms
- Name of the new char appears as a strip at the top of the buddy region
- Overlay vanishes after ~1.5s; GIF continues playing cleanly afterward

Repeat rights until the list wraps around to the original char.

- [ ] **Step 4: Smoke test — multi-char backward**

Push 5-way **left**. Expected: same behaviour as Step 3 but in reverse order.

- [ ] **Step 5: Smoke test — reboot persists selection**

With some char (say `bufo`) selected, reboot the device. Expected: after connect, Idle screen shows `bufo` again (not the original default).

- [ ] **Step 6: Smoke test — upload during session appears on next press**

While Idle is visible, have Claude Desktop push a brand-new char (e.g. `pudding`). Without rebooting, push right until the list wraps. Expected: `pudding` appears in the rotation.

- [ ] **Step 7: Smoke test — single-char overlay-only**

If possible, factory-reset and upload just one char (e.g. `bufo` only). Push left or right. Expected: GIF stays on `bufo`, but overlay "bufo" briefly appears.

If factory reset is inconvenient, skip this step and verify during the next spec iteration.

- [ ] **Step 8: Smoke test — zero-char no-op**

Factory reset (long-press 5-way → press A) so `/chars/` is empty. After reboot, on Idle (if any fallback UI shows), push left/right. Expected: nothing happens, no crash, no overlay.

- [ ] **Step 9: Smoke test — Left/Right ignored outside Idle**

In Advertising / Connected / Prompt modes, push left/right. Expected: no effect on any mode. (Prompt mode's A/C still approve/deny as before.)

- [ ] **Step 10: If any test fails, debug before merging**

For each failure, follow `superpowers:systematic-debugging`. Do not merge until all smoke tests pass.

- [ ] **Step 11: Merge to main**

```bash
git checkout main
git merge --no-ff feature/char-carousel -m "Merge feature/char-carousel: uploaded char carousel"
```

- [ ] **Step 12: Delete local feature branch**

```bash
git branch -d feature/char-carousel
```

- [ ] **Step 13: (Optional) Push to origin**

```bash
git push origin main
```

---

## Self-Review Notes

- **Spec coverage:** Each of the six "User Experience" points maps to Tasks 3/6/7. Edge cases in the spec (0/1/missing-current/upload-midsession) map to Task 3 tests and Task 9 smoke steps 6-8. "Press outside Idle ignored" is enforced by Task 7's `appState.mode == Mode::Idle` guard.
- **Placeholder scan:** No TBDs or "implement later" — each step has the exact code to write.
- **Type consistency:** `CarouselName`, `carouselEnumerate`, `carouselAdvance`, `characterRefreshManifest`, `BUDDY_OVERLAY_MS`, `CAROUSEL_MAX_CHARS`, `buddyOverlayUntilMs`, `buddyOverlayName[33]` — same spellings used in every task.
- **Task 3 → Task 5 coupling:** Task 3 adds a forward decl of `characterRefreshManifest` + no-op stub so it can land as a testable green commit before Task 5's real implementation. Task 5 removes the forward decl when it moves the declaration into `character.h`.
