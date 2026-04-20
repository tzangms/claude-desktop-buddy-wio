# SP6c — Built-in ASCII buddies + carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port 18 upstream ASCII buddies from `anthropics/claude-desktop-buddy` into firmware, add a 5-way left/right carousel that mixes built-in species with SP6a uploaded chars, and persist selection across reboots.

**Architecture:** New `src/buddy.{h,cpp}` owns the sprite framebuffer (`TFT_eSprite` 96×100, ~19 KB RAM), species registry, carousel index, and SFUD enumeration of uploaded chars. Species art files at `src/buddies/*.cpp` copy verbatim from upstream with two textual edits each (drop M5StickCPlus include + extern TFT_eSprite). Adapted layout constants shift the reference's 135-wide coordinate system to our 96×100 slot. `pet.cpp`'s ASCII frame arrays disappear; only its state machine survives in a new `pet_state.{h,cpp}`. Carousel selection reuses SP6a's `activeCharName[33]` persist field; species slugs (`_cat`, `_robot`, …) are reserved names for internal use.

**Tech Stack:** C++17, Arduino framework, PlatformIO, Seeed_Arduino_LCD (TFT_eSPI + TFT_eSprite), Seeed_Arduino_FS, SFUD, Unity native tests.

**Design spec:** `docs/superpowers/specs/2026-04-20-sp6c-builtin-buddies-design.md`

**Branch:** `feature/sp6c-builtin-buddies` off `main`. Merge via `--no-ff` when done.

**Upstream reference:** https://github.com/anthropics/claude-desktop-buddy

**File structure:**

| File | Responsibility |
|---|---|
| `src/buddy.h` (new) | Public API decls + `Species` struct |
| `src/buddy.cpp` (new) | Sprite, helpers, species registry, carousel, tick, /chars enumeration |
| `src/buddy_common.h` (new) | Layout constants + color palette + helper signatures |
| `src/buddies/*.cpp` × 18 (new) | One species per file, verbatim copy from upstream |
| `src/pet_state.h` / `src/pet_state.cpp` (new) | `PetState` enum + `petComputeState` + triggers + nap latch |
| `src/pet.h` / `src/pet.cpp` (delete) | ASCII frames replaced; state machine moved to pet_state |
| `src/buttons.h` / `src/buttons.cpp` (modify) | Add `PressLeft` / `PressRight` events |
| `src/main.cpp` (modify) | Drop character + pet block, add `buddyInit`, `buddyTick`, carousel button handler |
| `src/ui.cpp` (modify) | Drop ASCII pet render; add carousel label overlay |
| `src/xfer.cpp` (modify) | Reject reserved names |
| `src/config.h` (modify) | `BUDDY_OVERLAY_MS` |
| `platformio.ini` (modify) | Add `buddy.cpp` + `buddy_common` (if split) + `pet_state.cpp` to native `build_src_filter`; species files are ARDUINO-only |
| `test/test_buddy/test_buddy.cpp` (new) | Carousel + reserved-name native tests |

---

### Task 1: Branch + skeleton buddy module + sprite + build wiring

**Files:**
- Create: `src/buddy.h`, `src/buddy.cpp`, `src/buddy_common.h`
- Create: `test/test_buddy/test_buddy.cpp`
- Modify: `platformio.ini`
- Modify: `src/config.h`

- [ ] **Step 1: Branch**

```bash
git checkout main
git checkout -b feature/sp6c-builtin-buddies
```

- [ ] **Step 2: Write `src/buddy_common.h`**

```cpp
#pragma once
#include <cstdint>

// Shared constants for buddy species files. Adapted from upstream's
// 135×240 portrait layout to our 96×100 slot: BUDDY_X_CENTER / _Y_BASE
// re-anchored, rest kept at same offsets so species art stays legible.
extern const int BUDDY_X_CENTER;     // 48 — mid of 96-wide sprite
extern const int BUDDY_CANVAS_W;     // 96
extern const int BUDDY_Y_BASE;       // 30
extern const int BUDDY_Y_OVERLAY;    // 6
extern const int BUDDY_CHAR_W;       // 6
extern const int BUDDY_CHAR_H;       // 8

extern const uint16_t BUDDY_BG;
extern const uint16_t BUDDY_HEART;
extern const uint16_t BUDDY_DIM;
extern const uint16_t BUDDY_YEL;
extern const uint16_t BUDDY_WHITE;
extern const uint16_t BUDDY_CYAN;
extern const uint16_t BUDDY_GREEN;
extern const uint16_t BUDDY_PURPLE;
extern const uint16_t BUDDY_RED;
extern const uint16_t BUDDY_BLUE;

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff = 0);
void buddyPrintSprite(const char* const* lines, uint8_t nLines,
                      int yOffset, uint16_t color, int xOff = 0);
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char* s);
```

- [ ] **Step 3: Write `src/buddy.h`**

```cpp
#pragma once
#include <cstdint>
#include "pet_state.h"   // PetState

// Per-species state function: takes a tick counter, renders buddy body
// + overlay particles into the shared sprite.
typedef void (*StateFn)(uint32_t t);

struct Species {
  const char* slug;          // "cat", "robot", … (no underscore prefix)
  uint16_t    bodyColor;
  StateFn     states[7];     // indexed by B_SLEEP..B_HEART (upstream order)
};

// Called once at boot after manifestLoadActiveFromPersist. Sets up the
// sprite, enumerates /chars/*, restores the last-selected buddy from
// persist.
void buddyInit();

// Drive the currently-selected buddy. Call every loop iteration when
// Mode::Idle is visible.
void buddyTick(PetState st, uint32_t nowMs);

// renderIdle did fullRedraw; buddy region was wiped. Next tick repaints.
void buddyInvalidate();

// Carousel (no-op when not on Idle; caller guards).
void buddyNext();
void buddyPrev();
uint8_t     buddyCount();
uint8_t     buddyCurrentIdx();
const char* buddyCurrentName();

// Called by xfer.cpp after a successful char upload; re-enumerates /chars.
void buddyOnNewUpload();

#ifndef ARDUINO
void _buddyResetForTest();
#endif
```

- [ ] **Step 4: Write `src/buddy.cpp` skeleton**

```cpp
#include "buddy.h"
#include "buddy_common.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
#include <TFT_eSPI.h>
extern TFT_eSPI tft;  // defined in ui.cpp
TFT_eSprite spr(&tft);  // module-owned sprite framebuffer
#endif

// Layout constants — adapted to 96×100 slot.
const int      BUDDY_X_CENTER = 48;
const int      BUDDY_CANVAS_W = 96;
const int      BUDDY_Y_BASE   = 30;
const int      BUDDY_Y_OVERLAY = 6;
const int      BUDDY_CHAR_W   = 6;
const int      BUDDY_CHAR_H   = 8;
const uint16_t BUDDY_BG     = 0x0000;
const uint16_t BUDDY_HEART  = 0xF810;
const uint16_t BUDDY_DIM    = 0x8410;
const uint16_t BUDDY_YEL    = 0xFFE0;
const uint16_t BUDDY_WHITE  = 0xFFFF;
const uint16_t BUDDY_CYAN   = 0x07FF;
const uint16_t BUDDY_GREEN  = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED    = 0xF800;
const uint16_t BUDDY_BLUE   = 0x041F;

#ifdef ARDUINO
void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff) {
  int len = (int)std::strlen(line);
  int w = len * BUDDY_CHAR_W;
  int x = BUDDY_X_CENTER - w / 2 + xOff;
  spr.setTextColor(color, BUDDY_BG);
  spr.setCursor(x, yPx);
  for (int i = 0; i < len; i++) spr.print(line[i]);
}
void buddyPrintSprite(const char* const* lines, uint8_t nLines,
                      int yOffset, uint16_t color, int xOff) {
  spr.setTextSize(1);
  for (uint8_t i = 0; i < nLines; i++) {
    buddyPrintLine(lines[i], BUDDY_Y_BASE + yOffset + i * BUDDY_CHAR_H, color, xOff);
  }
}
void buddySetCursor(int x, int y) { spr.setCursor(x, y); }
void buddySetColor(uint16_t fg)    { spr.setTextColor(fg, BUDDY_BG); }
void buddyPrint(const char* s)     { spr.setTextSize(1); spr.print(s); }
#else
// Native stubs — tests verify registry / carousel, not rendering.
void buddyPrintLine(const char*, int, uint16_t, int) {}
void buddyPrintSprite(const char* const*, uint8_t, int, uint16_t, int) {}
void buddySetCursor(int, int) {}
void buddySetColor(uint16_t) {}
void buddyPrint(const char*) {}
#endif

namespace {
  uint8_t currentIdx = 0;
  // Species table populated in Task 2; initially empty → buddyCount 0.
  const Species* SPECIES_TABLE[] = { nullptr };
  constexpr uint8_t N_SPECIES = 0;
}

// Public stubs (filled in by later tasks).
void buddyInit() {}
void buddyTick(PetState, uint32_t) {}
void buddyInvalidate() {}
void buddyNext() {}
void buddyPrev() {}
uint8_t     buddyCount() { return N_SPECIES; }
uint8_t     buddyCurrentIdx() { return currentIdx; }
const char* buddyCurrentName() { return ""; }
void buddyOnNewUpload() {}

#ifndef ARDUINO
void _buddyResetForTest() { currentIdx = 0; }
#endif
```

- [ ] **Step 5: Append `BUDDY_OVERLAY_MS` to `src/config.h`**

After the existing SP6b constants:

```cpp
// --- SP6c carousel overlay ---
// "Name (k/n)" label shows this long after a buddy switch before fading.
static constexpr uint32_t BUDDY_OVERLAY_MS = 1500;
```

- [ ] **Step 6: Write `test/test_buddy/test_buddy.cpp` trivial test**

```cpp
#include <unity.h>
#include "buddy.h"

void test_count_zero_before_registry() {
  _buddyResetForTest();
  TEST_ASSERT_EQUAL_UINT8(0, buddyCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_count_zero_before_registry);
  return UNITY_END();
}
```

- [ ] **Step 7: Update `platformio.ini`**

Under `[env:native]` `build_src_filter`, append `+<buddy.cpp>`:

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet.cpp> +<xfer.cpp> +<manifest.cpp> +<character.cpp> +<buddy.cpp>
```

**Do not** add species files to the native filter (Task 4 pilots that). They are ARDUINO-only.

- [ ] **Step 8: Verify**

```bash
pio test -e native -f test_buddy
pio run -e seeed_wio_terminal
```

Expected: 1/1 PASSED; firmware SUCCESS. RAM slightly up (sprite allocation is lazy until `spr.createSprite()` in Task 2; skeleton here shouldn't push RAM yet).

- [ ] **Step 9: Commit**

```bash
git add src/buddy.h src/buddy.cpp src/buddy_common.h test/test_buddy/test_buddy.cpp src/config.h platformio.ini
git commit -m "$(cat <<'EOF'
sp6c: skeleton buddy module + sprite owner + test harness

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `pet_state` split + species registry + carousel API (native TDD)

Splits pet.cpp state-machine bits into a separate translation unit that
the buddy module can depend on, then fills in the carousel logic.

**Files:**
- Create: `src/pet_state.h`, `src/pet_state.cpp`
- Modify: `src/pet.h` (remove state-machine decls), `src/pet.cpp` (remove state-machine impl)
- Modify: `src/buddy.cpp` (carousel + registry)
- Modify: `test/test_buddy/test_buddy.cpp` (carousel tests)
- Modify: `platformio.ini` (add `pet_state.cpp` to native filter)

- [ ] **Step 1: Write `src/pet_state.h`**

```cpp
#pragma once

#include <cstdint>

struct AppState;

enum class PetState {
  Sleep,
  Idle,
  Busy,
  Attention,
  Celebrate,
  Heart,
  Dizzy,
  Nap,
};

PetState petComputeState(const AppState& s, uint32_t nowMs);

void petTriggerCelebrate(uint32_t nowMs);
void petTriggerHeart(uint32_t nowMs);
void petTriggerDizzy(uint32_t nowMs);

void petEnterNap();
void petExitNap();
bool petIsNapping();
```

- [ ] **Step 2: Write `src/pet_state.cpp`**

Copy the state-machine impl from the current `src/pet.cpp` (the
`petComputeState`, `petTriggerX`, `petEnterNap/Exit/IsNapping`
functions + the anonymous namespace timer state). Do NOT copy the
`FRAMES_*` arrays or `petFace/petTickFrame/petCurrentFrame/petResetFrame`.

The result has the same functions but is about 70 lines (was 110+).

- [ ] **Step 3: Delete state-machine bits from `src/pet.h`**

Remove declarations of `petComputeState`, `petTriggerX`, nap functions.
Keep only the frame-related bits for now (to avoid a temporary compile
break — they stay until Task 6 wires buddy into renderIdle):

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include "pet_state.h"   // re-export PetState + trigger API

static constexpr size_t PET_FACE_LINES = 4;
static constexpr size_t PET_FRAMES_PER_STATE = 3;

const char* const* petFace(PetState state, size_t frameIdx);
bool petTickFrame(uint32_t nowMs);
size_t petCurrentFrame();
void petResetFrame(uint32_t nowMs);
```

- [ ] **Step 4: Delete state-machine impl from `src/pet.cpp`**

Remove the functions now in `pet_state.cpp`. Keep the frame arrays
+ `petFace` / `petTickFrame` / `petCurrentFrame` / `petResetFrame`.

- [ ] **Step 5: Verify native build + existing tests green after split**

```bash
pio test -e native
```

Expected: all existing tests still PASS. `test_buddy` count stays at 1.

- [ ] **Step 6: Add `pet_state.cpp` to native filter**

```
build_src_filter = ... +<pet_state.cpp>
```

(Native already has `pet.cpp` in the filter — keep both for now; Task
6 deletes `pet.cpp` entirely.)

- [ ] **Step 7: Build firmware, verify no regressions**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM unchanged.

- [ ] **Step 8: Commit the pet_state split**

```bash
git add src/pet_state.h src/pet_state.cpp src/pet.h src/pet.cpp platformio.ini
git commit -m "$(cat <<'EOF'
sp6c: split pet_state out of pet for buddy module dependency

Carves PetState + petComputeState + the trigger / nap API into a new
pet_state.{h,cpp} so the buddy module (Task 3+) can include it
without pulling in the ASCII frame arrays that SP6c will delete.
pet.{h,cpp} keeps the frame-rendering bits for one more task so
renderIdle compiles; Task 6 removes them atomically with the buddy
wire-up.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 9: Add failing carousel tests**

Append to `test/test_buddy/test_buddy.cpp`:

```cpp
#include <cstring>

// Stub species functions for carousel tests (native can't paint but
// the registry just needs non-null pointers).
static void stubState(uint32_t) {}

// Two fake species used as the native registry.
extern const Species FAKE_AXOLOTL_SPECIES;
extern const Species FAKE_CAT_SPECIES;

// External linkage so buddy.cpp's native table can resolve them.
const Species FAKE_AXOLOTL_SPECIES{
  "axolotl", 0xFFFF,
  { stubState, stubState, stubState, stubState, stubState, stubState, stubState }
};
const Species FAKE_CAT_SPECIES{
  "cat", 0xFFFF,
  { stubState, stubState, stubState, stubState, stubState, stubState, stubState }
};

void test_count_after_registry() {
  _buddyResetForTest();
  TEST_ASSERT_GREATER_OR_EQUAL_UINT8(2, buddyCount());
}

void test_initial_current_is_zero() {
  _buddyResetForTest();
  TEST_ASSERT_EQUAL_UINT8(0, buddyCurrentIdx());
}

void test_next_advances_and_wraps() {
  _buddyResetForTest();
  uint8_t n = buddyCount();
  for (uint8_t i = 0; i < n; i++) buddyNext();
  TEST_ASSERT_EQUAL_UINT8(0, buddyCurrentIdx());
}

void test_prev_wraps_from_zero() {
  _buddyResetForTest();
  buddyPrev();
  TEST_ASSERT_EQUAL_UINT8(buddyCount() - 1, buddyCurrentIdx());
}

void test_current_name_is_slug() {
  _buddyResetForTest();
  TEST_ASSERT_EQUAL_STRING("axolotl", buddyCurrentName());
  buddyNext();
  TEST_ASSERT_EQUAL_STRING("cat", buddyCurrentName());
}
```

Register them in `main`:

```cpp
RUN_TEST(test_count_after_registry);
RUN_TEST(test_initial_current_is_zero);
RUN_TEST(test_next_advances_and_wraps);
RUN_TEST(test_prev_wraps_from_zero);
RUN_TEST(test_current_name_is_slug);
```

- [ ] **Step 10: Run — RED**

```bash
pio test -e native -f test_buddy
```

Expected: 5 new tests FAIL (registry empty).

- [ ] **Step 11: Wire the fake-species table for native builds + real registry for ARDUINO**

In `src/buddy.cpp`, replace the placeholder table with two paths:

```cpp
#ifdef ARDUINO
// Real species extern'd from src/buddies/*.cpp (Task 3+ populates).
// Listed alphabetically so carousel order is predictable.
extern const Species AXOLOTL_SPECIES;
extern const Species BLOB_SPECIES;
extern const Species CACTUS_SPECIES;
extern const Species CAPYBARA_SPECIES;
extern const Species CAT_SPECIES;
extern const Species CHONK_SPECIES;
extern const Species DRAGON_SPECIES;
extern const Species DUCK_SPECIES;
extern const Species GHOST_SPECIES;
extern const Species GOOSE_SPECIES;
extern const Species MUSHROOM_SPECIES;
extern const Species OCTOPUS_SPECIES;
extern const Species OWL_SPECIES;
extern const Species PENGUIN_SPECIES;
extern const Species RABBIT_SPECIES;
extern const Species ROBOT_SPECIES;
extern const Species SNAIL_SPECIES;
extern const Species TURTLE_SPECIES;

static const Species* kSpeciesTable[] = {
  &AXOLOTL_SPECIES, &BLOB_SPECIES, &CACTUS_SPECIES, &CAPYBARA_SPECIES,
  &CAT_SPECIES, &CHONK_SPECIES, &DRAGON_SPECIES, &DUCK_SPECIES,
  &GHOST_SPECIES, &GOOSE_SPECIES, &MUSHROOM_SPECIES, &OCTOPUS_SPECIES,
  &OWL_SPECIES, &PENGUIN_SPECIES, &RABBIT_SPECIES, &ROBOT_SPECIES,
  &SNAIL_SPECIES, &TURTLE_SPECIES,
};
#else
// Native tests use a minimal fake table defined in test_buddy.cpp and
// linked here via two externs.
extern const Species FAKE_AXOLOTL_SPECIES;
extern const Species FAKE_CAT_SPECIES;
static const Species* kSpeciesTable[] = { &FAKE_AXOLOTL_SPECIES, &FAKE_CAT_SPECIES };
#endif

static constexpr uint8_t kSpeciesCount =
    sizeof(kSpeciesTable) / sizeof(kSpeciesTable[0]);
```

Replace the `buddyCount` / `buddyCurrentIdx` / `buddyCurrentName` /
`buddyNext` / `buddyPrev` stubs:

```cpp
uint8_t buddyCount() { return kSpeciesCount; }
uint8_t buddyCurrentIdx() { return currentIdx; }
const char* buddyCurrentName() {
  return kSpeciesCount ? kSpeciesTable[currentIdx]->slug : "";
}
void buddyNext() {
  if (!kSpeciesCount) return;
  currentIdx = (currentIdx + 1) % kSpeciesCount;
}
void buddyPrev() {
  if (!kSpeciesCount) return;
  currentIdx = (uint8_t)((currentIdx + kSpeciesCount - 1) % kSpeciesCount);
}
```

Remove the placeholder `constexpr uint8_t N_SPECIES = 0;` from earlier.

- [ ] **Step 12: Run — GREEN**

```bash
pio test -e native -f test_buddy
```

Expected: 6/6 PASS.

Native build will link against `FAKE_*_SPECIES` from `test_buddy.cpp`.
ARDUINO build will fail to link (the 18 species don't exist yet) —
that's expected; Task 3/4 provide them. Don't run firmware build yet.

- [ ] **Step 13: Commit**

```bash
git add src/buddy.cpp test/test_buddy/test_buddy.cpp
git commit -m "$(cat <<'EOF'
sp6c: species table + carousel index (native-TDD)

Buddy carousel cycles an alphabetical species slug list; native tests
use two FAKE species defined in test_buddy.cpp. ARDUINO linkage
depends on Task 3/4 populating the real 18-species externs — don't
build firmware until then.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Pilot species port (cat) + validate sprite pipeline on device

Ports ONE species file to shake out adaptation issues (geometry,
namespaces, includes) before bulk copying 17 more.

**Files:**
- Create: `src/buddies/cat.cpp`
- Modify: `src/buddy.cpp` (enable sprite create + tick dispatch)
- Modify: `platformio.ini` (ARDUINO env picks up `src/buddies/*.cpp`)

- [ ] **Step 1: Download cat.cpp from upstream**

```bash
mkdir -p src/buddies
curl -s https://raw.githubusercontent.com/anthropics/claude-desktop-buddy/main/src/buddies/cat.cpp > src/buddies/cat.cpp
wc -l src/buddies/cat.cpp  # expect ~300-350 lines
```

- [ ] **Step 2: Edit cat.cpp for our framework**

Open `src/buddies/cat.cpp` and apply these textual edits:

1. Replace `#include "../buddy.h"` → **keep** (we have the same path).
2. Replace `#include "../buddy_common.h"` → **keep**.
3. Replace `#include <M5StickCPlus.h>` → **delete**.
4. Remove the line `extern TFT_eSprite spr;` (our buddy.cpp owns it
   internally; species don't need to see it — they use the helpers).
5. At the very bottom of the file (after the last `}` closing the
   `cat` namespace), keep (or add if missing) the `Species` export:

```cpp
}   // namespace cat

const Species CAT_SPECIES = {
  "cat", BUDDY_WHITE,
  { cat::doSleep, cat::doIdle, cat::doBusy, cat::doAttention,
    cat::doCelebrate, cat::doDizzy, cat::doHeart }
};
```

Upstream typically already has this — verify and adapt the trailing
block if needed. If the color constant is different (e.g. `BUDDY_YEL`),
leave upstream's choice.

- [ ] **Step 3: Enable sprite creation + tick dispatch in `src/buddy.cpp`**

Update `buddyInit` / `buddyTick`:

```cpp
#ifdef ARDUINO
void buddyInit() {
  spr.setColorDepth(16);
  spr.createSprite(BUDDY_W, BUDDY_H);   // BUDDY_W/H from config.h
  spr.fillSprite(BUDDY_BG);
  spr.setTextSize(1);
}

void buddyTick(PetState st, uint32_t nowMs) {
  if (!kSpeciesCount) return;
  // Upstream runs animations at ~5 fps via TICK_MS=200.
  static uint32_t nextTickAt = 0;
  static uint32_t tickCount  = 0;
  if (nowMs < nextTickAt) return;
  nextTickAt = nowMs + 200;
  tickCount++;

  const Species* sp = kSpeciesTable[currentIdx];
  // PetState → species state index (upstream enum B_SLEEP..B_HEART = 0..6).
  uint8_t ordinal;
  switch (st) {
    case PetState::Sleep:     ordinal = 0; break;
    case PetState::Idle:      ordinal = 1; break;
    case PetState::Busy:      ordinal = 2; break;
    case PetState::Attention: ordinal = 3; break;
    case PetState::Celebrate: ordinal = 4; break;
    case PetState::Dizzy:     ordinal = 5; break;
    case PetState::Heart:     ordinal = 6; break;
    case PetState::Nap:       ordinal = 0; break;  // fall back to sleep
  }
  spr.fillSprite(BUDDY_BG);
  sp->states[ordinal](tickCount);
  spr.pushSprite(BUDDY_X, BUDDY_Y);
}

void buddyInvalidate() {
  // Next buddyTick repaints the whole sprite anyway (fillSprite every tick).
  // No cache to invalidate.
}
#else
void buddyInit() {}
void buddyTick(PetState, uint32_t) {}
void buddyInvalidate() {}
#endif
```

- [ ] **Step 4: Update `platformio.ini` ARDUINO env** (if needed)

The default ARDUINO `build_src_filter` picks up `src/**/*.cpp` already;
no change needed. Verify by grepping.

Actually — check: default filter is `+<*>` which expands to all `.cpp`
in `src/`. Subdirectories: depends on platformio version. Test by
building and checking cat.cpp appears in the build log.

If not picked up, add to `[env:seeed_wio_terminal]`:

```
build_src_filter = +<*> +<buddies/>
```

- [ ] **Step 5: Also wire one quick sanity call in `main.cpp::setup`**

Temporarily, so we can see cat rendering on device before all wiring
is done. After `characterInit();` add:

```cpp
  buddyInit();
```

In `loop()`, before `delay(10)`, add a temporary test-tick:

```cpp
  if (appState.mode == Mode::Idle) {
    buddyTick(petComputeState(appState, millis()), millis());
  }
```

This overlaps with the character block in Task 6; the temp version
will be rewritten there. For now it lets us validate the sprite works.

- [ ] **Step 6: Build firmware**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM ≥ 58 % (sprite + existing usage). Flash
+~12 KB for cat.cpp strings.

- [ ] **Step 7: Flash + device smoke for cat**

```bash
pio run -e seeed_wio_terminal -t upload
```

Device reboots. On Idle screen (after connecting + heartbeat), the
buddy slot should show CAT ASCII art animating. If characterReady
(bufo uploaded), there's a conflict with character.cpp painting —
expected, Task 6 resolves by removing the character block. For now,
note whether cat's body shows at least briefly between character
frames.

- [ ] **Step 8: Visual checklist**

- Body centered in the 96×100 slot (head, face, body visible).
- Sleep state animates over ~12 s (breathing, blinking, Z particles
  drift).
- Idle state shows micro-actions (look L/R, blink, tail flicks).
- No text bleeds past x=96 or y=100 (the sprite edges clip).

If a species is too wide/tall for our 96×100 (upstream designed for
a 135×240 canvas), note the clipping in the task's self-review; the
bulk port in Task 4 needs to inspect each.

- [ ] **Step 9: Commit**

```bash
git add src/buddies/cat.cpp src/buddy.cpp src/main.cpp platformio.ini
git commit -m "$(cat <<'EOF'
sp6c: pilot cat species port + sprite createSprite + tick dispatch

Copies upstream cat.cpp with three textual edits (drop M5StickCPlus
include, drop extern TFT_eSprite, verify/add CAT_SPECIES export).
Buddy tick runs at ~5 fps (TICK_MS=200) matching upstream, fills
sprite with BUDDY_BG each frame, dispatches to species state fn,
pushSprite to (BUDDY_X, BUDDY_Y). Temporary buddyTick wiring in
main::loop for pilot — Task 6 replaces with final wiring that
coexists with the character GIF path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Bulk port remaining 17 species + verify each on device

Copy the other 17 species files from upstream with the same three
edits. Verify each renders cleanly.

**Files:**
- Create: `src/buddies/{axolotl,blob,cactus,capybara,chonk,dragon,duck,ghost,goose,mushroom,octopus,owl,penguin,rabbit,robot,snail,turtle}.cpp`

- [ ] **Step 1: Bulk download**

```bash
for name in axolotl blob cactus capybara chonk dragon duck ghost goose \
            mushroom octopus owl penguin rabbit robot snail turtle; do
  curl -s "https://raw.githubusercontent.com/anthropics/claude-desktop-buddy/main/src/buddies/${name}.cpp" \
    > "src/buddies/${name}.cpp"
  wc -l "src/buddies/${name}.cpp"
done
```

Each file should be 250-400 lines.

- [ ] **Step 2: Apply edits to each**

For every file in `src/buddies/` except cat.cpp (already done):

1. Delete `#include <M5StickCPlus.h>`.
2. Delete `extern TFT_eSprite spr;`.
3. Confirm the trailing `const Species FOO_SPECIES = { ... };` export
   exists and matches the expected name (`AXOLOTL_SPECIES`, etc.).

A one-liner to apply (1) and (2) mechanically:

```bash
for f in src/buddies/*.cpp; do
  sed -i '' '/^#include <M5StickCPlus.h>$/d' "$f"
  sed -i '' '/^extern TFT_eSprite spr;$/d' "$f"
done
```

Then open each file, check the `const Species X_SPECIES = {...}` block
at the bottom. For files where upstream has different formatting, add
the export block manually matching cat.cpp's pattern.

- [ ] **Step 3: Build firmware**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. Flash ≈ 50-55 % (18 × ~12 KB strings).

If a species fails to compile, the most common causes are:
- Missing `const Species X_SPECIES` export at the end of the file.
- Upstream file uses a helper name that drifted from our
  `buddy_common.h` (unlikely — upstream's helpers are stable).
- Unicode characters in ASCII art that need `\\` escaping (cp-437 /
  UTF-8 mixups).

Fix per file.

- [ ] **Step 4: Flash + visual walk through all 18**

```bash
pio run -e seeed_wio_terminal -t upload
```

For this task the carousel buttons aren't wired yet (Task 5). To
iterate species visually, temporarily set the default in buddy.cpp's
`static uint8_t currentIdx = 0;` to each index in turn and re-flash.
Alternative: cycle via Serial command if fastest — skip if not worth
the tooling.

For each of 18 species, verify on the device:

- Body is centered in the 96×100 slot.
- Sleep / Idle states animate without visible break.
- Particles (Zs, hearts, sparks) stay on-slot (may be clipped at
  edges for species designed wider than 96 — acceptable).

Record per-species notes in commit body.

- [ ] **Step 5: Commit**

```bash
git add src/buddies/
git commit -m "$(cat <<'EOF'
sp6c: port remaining 17 species from upstream

Verbatim copies with two textual edits each (drop M5StickCPlus
include + extern TFT_eSprite). Species list alphabetical: axolotl,
blob, cactus, capybara, chonk, dragon, duck, ghost, goose, mushroom,
octopus, owl, penguin, rabbit, robot, snail, turtle. All 18 now link
and render on device; minor edge-clipping on species designed for
upstream's 135-wide canvas is accepted.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `PressLeft` / `PressRight` button events

**Files:**
- Modify: `src/buttons.h`, `src/buttons.cpp`

- [ ] **Step 1: Update `src/buttons.h`**

Append to the `ButtonEvent` enum after `PressC`:

```cpp
enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,
  PressLeft,
  PressRight,
  LongPressNav,
};
```

- [ ] **Step 2: Update `src/buttons.cpp` to poll 5-way Left/Right**

Find the `btns[]` array. Add two entries after `WIO_5S_PRESS`:

```cpp
{WIO_KEY_A,    true, true, 0, ButtonEvent::PressA},
{WIO_KEY_B,    true, true, 0, ButtonEvent::PressB},
{WIO_KEY_C,    true, true, 0, ButtonEvent::PressC},
{WIO_5S_PRESS, true, true, 0, ButtonEvent::PressNav},
{WIO_5S_LEFT,  true, true, 0, ButtonEvent::PressLeft},
{WIO_5S_RIGHT, true, true, 0, ButtonEvent::PressRight},
```

The Wio Terminal pinmap provides `WIO_5S_LEFT` and `WIO_5S_RIGHT`
via its board variant. If they're not defined (pre-existing issue),
add pullup init in `initButtons()` matching the pattern for other pins.

- [ ] **Step 3: Build firmware, verify no link errors**

```bash
pio run -e seeed_wio_terminal
```

- [ ] **Step 4: Commit**

```bash
git add src/buttons.h src/buttons.cpp
git commit -m "$(cat <<'EOF'
sp6c: add PressLeft / PressRight button events

Plumbs WIO_5S_LEFT and WIO_5S_RIGHT through the same debounced poll
path as A/B/C/Nav. No behavior change yet — carousel hooks up in
Task 6.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Wire buddy into main + ui, delete `pet.cpp` ASCII

**Files:**
- Modify: `src/main.cpp`, `src/ui.cpp`, `src/pet.h`, `src/pet.cpp` (delete)
- Modify: `platformio.ini` (drop `pet.cpp` from native filter)

- [ ] **Step 1: Replace main::loop's character block with buddy dispatch**

In `src/main.cpp::loop`, replace the entire Idle-character block
(the one that tracks `lastCharMode / lastCharState / lastCharInit`
and calls `characterSetState` / `characterTick`) with:

```cpp
  if (appState.mode == Mode::Idle) {
    PetState st = petComputeState(appState, now);
    buddyTick(st, now);
  }
```

Delete the removed character block (but keep the `#include "character.h"`
for now — Task 8's upload hook still uses it).

- [ ] **Step 2: Handle carousel buttons in main::loop**

In the button-handler section, after the existing `ButtonEvent::PressA /
PressC` branch (which handles permission decisions), add:

```cpp
    else if ((e == ButtonEvent::PressLeft || e == ButtonEvent::PressRight) &&
             appState.mode == Mode::Idle) {
      if (e == ButtonEvent::PressLeft) buddyPrev();
      else                              buddyNext();
      persistSetActiveChar(buddyCurrentName());
      // Trigger a brief "Name (k/n)" overlay on the next render pass.
      appState.buddyOverlayUntilMs = now + BUDDY_OVERLAY_MS;
      pendingRender = true;
    }
```

You'll add `buddyOverlayUntilMs` to `AppState` in the next step.

- [ ] **Step 3: Add `buddyOverlayUntilMs` to AppState**

In `src/state.h`, append to `AppState`:

```cpp
uint32_t buddyOverlayUntilMs = 0;   // 0 = no overlay; else expiry
```

- [ ] **Step 4: Drop ASCII pet render in `src/ui.cpp::renderIdle`**

Find the existing ASCII pet block (guarded by
`if (!characterReady() && (st != lastPet || frame != lastFrame))`).
Delete the entire block plus its `static PetState lastPet` /
`static size_t lastFrame` statics at the top of `renderIdle`.

The buddy slot is now owned exclusively by `buddyTick` (non-buddy
screens don't paint it).

- [ ] **Step 5: Add carousel overlay painter in `src/ui.cpp::renderIdle`**

Near the end of `renderIdle` (before the `drawFooter` block), add:

```cpp
  static uint32_t lastOverlayUntilMs = 0;
  if (s.buddyOverlayUntilMs != lastOverlayUntilMs) {
    lastOverlayUntilMs = s.buddyOverlayUntilMs;
    uint32_t now = millis();
    if (s.buddyOverlayUntilMs > now) {
      // "Name (k/n)" at the top of the buddy slot for BUDDY_OVERLAY_MS.
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%s (%d/%d)",
                    buddyCurrentName(), buddyCurrentIdx() + 1, buddyCount());
      tft.fillRect(BUDDY_X, BUDDY_Y - 10, BUDDY_W, 10, COLOR_BG);
      tft.setTextColor(COLOR_FG, COLOR_BG);
      tft.setTextSize(1);
      tft.setCursor(BUDDY_X, BUDDY_Y - 10);
      tft.print(buf);
    } else {
      // Expired — clear the label band.
      tft.fillRect(BUDDY_X, BUDDY_Y - 10, BUDDY_W, 10, COLOR_BG);
    }
  }
```

Add `#include "buddy.h"` at the top if not already present.

- [ ] **Step 6: In main::loop, invalidate the overlay on expiry**

After the carousel button handler and anywhere the overlay could
expire, schedule a re-render when the timer passes:

```cpp
  if (appState.buddyOverlayUntilMs && now >= appState.buddyOverlayUntilMs) {
    appState.buddyOverlayUntilMs = 0;
    pendingRender = true;
  }
```

Put this near the existing `applyTimeouts` call.

- [ ] **Step 7: Delete `src/pet.h` / `src/pet.cpp` frame bits, update includes**

In `src/pet.h`, keep only the re-export of `pet_state.h` decls:

```cpp
#pragma once
#include "pet_state.h"   // re-export; no frame API anymore
```

…or delete `pet.h` entirely and sweep `#include "pet.h"` → `#include
"pet_state.h"` across the codebase. The latter is cleaner.

Delete `src/pet.cpp` (its state-machine was moved to pet_state.cpp in
Task 2; the ASCII-frame bits go away with the buddy system).

- [ ] **Step 8: Update `platformio.ini` native filter to drop `pet.cpp`**

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet_state.cpp> +<xfer.cpp> +<manifest.cpp> +<character.cpp> +<buddy.cpp>
```

- [ ] **Step 9: Build firmware + native tests, verify no regressions**

```bash
pio test -e native
pio run -e seeed_wio_terminal
```

Expected: all native tests PASS (likely ~140 now). Firmware SUCCESS.

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp src/ui.cpp src/state.h platformio.ini
git rm src/pet.cpp src/pet.h
git commit -m "$(cat <<'EOF'
sp6c: wire buddy to main::loop + renderIdle, drop pet.cpp ASCII

Idle screen's buddy slot is now owned exclusively by buddyTick,
which dispatches the currently-selected species' state function each
~200 ms. Left/Right on the 5-way cycle the carousel and persist
the new slug via persistSetActiveChar. A 1.5 s "Name (k/n)" overlay
above the buddy slot confirms the switch.

pet.cpp's ASCII frame arrays + petFace/petTickFrame API deleted.
Re-exports live in pet.h → pet_state.h (or pet.h deleted entirely
depending on sweep outcome).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: `xferIsValidName` reserved-name enforcement (native TDD)

**Files:**
- Modify: `src/xfer.cpp`
- Modify: `test/test_xfer/test_xfer.cpp`

- [ ] **Step 1: Add failing tests**

At the bottom of `test/test_xfer/test_xfer.cpp` (before `main`):

```cpp
void test_rejects_underscore_prefix() {
  TEST_ASSERT_FALSE(xferIsValidName("_cat"));
  TEST_ASSERT_FALSE(xferIsValidName("_anything"));
}

void test_rejects_exact_species_slug() {
  TEST_ASSERT_FALSE(xferIsValidName("cat"));
  TEST_ASSERT_FALSE(xferIsValidName("robot"));
  TEST_ASSERT_FALSE(xferIsValidName("dragon"));
}

void test_accepts_unrelated_name() {
  TEST_ASSERT_TRUE(xferIsValidName("bufo"));
  TEST_ASSERT_TRUE(xferIsValidName("mypet"));
}
```

Register them in `main`.

- [ ] **Step 2: Run — RED**

```bash
pio test -e native -f test_xfer
```

Expected: `test_rejects_underscore_prefix` + `test_rejects_exact_species_slug`
FAIL. The third (unrelated name) passes.

- [ ] **Step 3: Implement rejection in `src/xfer.cpp::xferIsValidName`**

At the top of xfer.cpp (after includes), add:

```cpp
namespace {
  // Kept in sync with buddy.cpp's species registry. Any upload whose
  // name collides with a built-in species slug would break the
  // carousel's name → slot resolution.
  constexpr const char* kReservedSpeciesSlugs[] = {
    "axolotl", "blob", "cactus", "capybara", "cat", "chonk", "dragon",
    "duck", "ghost", "goose", "mushroom", "octopus", "owl", "penguin",
    "rabbit", "robot", "snail", "turtle",
  };
  constexpr size_t kReservedSpeciesCount =
    sizeof(kReservedSpeciesSlugs) / sizeof(kReservedSpeciesSlugs[0]);
}
```

Extend `xferIsValidName`. Find the existing body:

```cpp
bool xferIsValidName(const char* s) {
  if (!s || !*s) return false;
  size_t len = std::strlen(s);
  if (len >= 64) return false;
  if (s[0] == '.') return false;
  ...
```

Add underscore-prefix and species-slug rejection near the top, right
after the dot check:

```cpp
  if (s[0] == '_') return false;              // reserved for built-in species
  for (size_t i = 0; i < kReservedSpeciesCount; ++i) {
    if (std::strcmp(s, kReservedSpeciesSlugs[i]) == 0) return false;
  }
```

- [ ] **Step 4: Run — GREEN**

```bash
pio test -e native -f test_xfer
```

Expected: all tests PASS.

- [ ] **Step 5: Full native sweep + firmware build**

```bash
pio test -e native
pio run -e seeed_wio_terminal
```

Expected: all green.

- [ ] **Step 6: Commit**

```bash
git add src/xfer.cpp test/test_xfer/test_xfer.cpp
git commit -m "$(cat <<'EOF'
sp6c: reject reserved names on char upload

xferIsValidName now rejects names starting with '_' (reserved for
built-in species) and names exactly matching one of the 18 species
slugs. Keeps the carousel's name-based resolution unambiguous.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: `/chars` enumeration + mid-session refresh + persist rehydrate

**Files:**
- Modify: `src/buddy.cpp`
- Modify: `src/xfer.cpp` (hook after successful `char_end`)

- [ ] **Step 1: Implement `/chars` enumeration in `buddyInit` (ARDUINO-only)**

Replace the sprite-only `buddyInit` body:

```cpp
#ifdef ARDUINO
static char uploadedChars[16][33];
static uint8_t uploadedCount = 0;

static void enumerateUploaded() {
  uploadedCount = 0;
  File root = SFUD.open("/chars");
  if (!root || !root.isDirectory()) return;
  File entry = root.openNextFile();
  while (entry && uploadedCount < 16) {
    if (entry.isDirectory()) {
      const char* n = entry.name();
      const char* slash = std::strrchr(n, '/');
      const char* base = slash ? slash + 1 : n;
      std::strncpy(uploadedChars[uploadedCount], base, 32);
      uploadedChars[uploadedCount][32] = '\0';
      uploadedCount++;
    }
    entry = root.openNextFile();
  }
  root.close();
}

void buddyInit() {
  spr.setColorDepth(16);
  spr.createSprite(BUDDY_W, BUDDY_H);
  spr.fillSprite(BUDDY_BG);
  spr.setTextSize(1);
  enumerateUploaded();

  // Resolve persisted activeCharName to carousel index.
  const char* name = persistGetActiveChar();
  if (name && name[0] != '\0') {
    // Species slug path ("_cat" etc. → try without underscore).
    if (name[0] == '_') {
      for (uint8_t i = 0; i < kSpeciesCount; ++i) {
        if (std::strcmp(kSpeciesTable[i]->slug, name + 1) == 0) {
          currentIdx = i;
          return;
        }
      }
    }
    // Uploaded char path.
    for (uint8_t i = 0; i < uploadedCount; ++i) {
      if (std::strcmp(uploadedChars[i], name) == 0) {
        currentIdx = kSpeciesCount + i;
        return;
      }
    }
  }
  currentIdx = 0;
}

void buddyOnNewUpload() { enumerateUploaded(); }
#endif
```

The native stubs (`buddyInit() {}`, `buddyOnNewUpload() {}`) stay as-is.

- [ ] **Step 2: Update `buddyCount` + `buddyCurrentName` to include uploaded**

Replace:

```cpp
uint8_t buddyCount() {
#ifdef ARDUINO
  return kSpeciesCount + uploadedCount;
#else
  return kSpeciesCount;
#endif
}

const char* buddyCurrentName() {
#ifdef ARDUINO
  if (currentIdx < kSpeciesCount) {
    // Species slug encoded with leading underscore for persist.
    static char buf[34];
    std::snprintf(buf, sizeof(buf), "_%s", kSpeciesTable[currentIdx]->slug);
    return buf;
  }
  uint8_t u = currentIdx - kSpeciesCount;
  if (u >= uploadedCount) return "";
  return uploadedChars[u];
#else
  if (!kSpeciesCount) return "";
  return kSpeciesTable[currentIdx]->slug;
#endif
}
```

Native path returns raw slug (no leading `_`) so native tests
still verify `"axolotl"` / `"cat"` directly.

- [ ] **Step 3: Update `buddyTick` dispatch to handle uploaded chars**

```cpp
void buddyTick(PetState st, uint32_t nowMs) {
  if (currentIdx < kSpeciesCount) {
    // Built-in species path (unchanged from Task 3).
    ...
  } else {
    // Uploaded char path — delegate to the SP6b character module.
    // First tick after switching: manifestSetActive + characterSetState.
    static uint8_t lastActiveIdx = 0xFF;
    if (currentIdx != lastActiveIdx) {
      lastActiveIdx = currentIdx;
      uint8_t u = currentIdx - kSpeciesCount;
      if (u < uploadedCount) {
        manifestSetActive(uploadedChars[u]);
        characterInit();
        characterSetState(st);
      }
    }
    if (characterReady()) characterTick(nowMs);
  }
}
```

Add `#include "character.h"` + `#include "manifest.h"` at the top
of buddy.cpp.

- [ ] **Step 4: Hook `buddyOnNewUpload` into `xferEndChar`**

In `src/xfer.cpp::xferEndChar` ARDUINO body, after
`persistSetActiveChar(charName);`:

```cpp
#ifdef ARDUINO
bool xferEndChar() {
  ...
#ifdef ARDUINO
  if (!manifestSetActive(charName)) return false;
  persistSetActiveChar(charName);
  buddyOnNewUpload();
#endif
  return true;
}
```

Add `#include "buddy.h"` at the top of xfer.cpp under the ARDUINO
include block.

- [ ] **Step 5: Build firmware**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS.

- [ ] **Step 6: Device smoke for uploaded integration**

Flash. Upload bufo via Hardware Buddy. After `char_end` succeeds:

- Right-press multiple times until reaching `bufo` in the carousel
  (past all 18 species). Overlay shows `bufo (19/19)`.
- Buddy slot switches to bufo GIF animation.
- Right-press again: wraps back to `axolotl`.
- Reset device: on boot, bufo re-selects if last-used.

- [ ] **Step 7: Commit**

```bash
git add src/buddy.cpp src/xfer.cpp
git commit -m "$(cat <<'EOF'
sp6c: enumerate /chars, rehydrate from persist, refresh on upload

buddyInit scans /chars/* via SFUD.openNextFile and caches up to 16
uploaded-char dir names. Species (underscore-prefixed slug in persist)
and uploaded-char (raw name) selections both resolve to a carousel
index. buddyTick delegates the uploaded-char branch to character.cpp
(SP6b). xferEndChar invokes buddyOnNewUpload after a successful
char_end so newly uploaded chars appear in the carousel without a
reboot.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Device smoke + merge

**Files:** none (verification + merge only).

- [ ] **Step 1: Full native test sweep**

```bash
pio test -e native
```

Expected: all tests PASS (current count + ~6 new buddy tests + 3 xfer
reserved-name tests = ~140).

- [ ] **Step 2: Final firmware build**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM ~58 %. Flash ~55 %.

- [ ] **Step 3: Flash + end-to-end smoke**

Put Wio Terminal in bootloader, then:

```bash
pio run -e seeed_wio_terminal -t upload
```

On the device, verify:

1. Fresh device (factory reset via long-press + A to confirm) →
   on boot, buddy slot shows `axolotl` idle animation (index 0).
2. Right-press 5x → cycles through species, overlay shows each name.
3. Left-press 1x → wraps / goes back.
4. Connect Hardware Buddy → heartbeat arrives → state machine is
   driven correctly (busy state when running>0, attention on prompt).
5. Upload `characters/bufo` → after `char_end`, right-press to cycle
   past 18 species → lands on `bufo` which animates as GIF.
6. Physical reset → last-selected buddy persists.
7. Press A / C during a prompt → permission approve / deny work,
   heart/celebrate reactions animate on the current buddy.
8. Left / right during prompt → ignored (no carousel switch while
   permission is pending).

Record any visual regressions per species.

- [ ] **Step 4: Commit any last smoke-found fixes**

If any species needs a per-file adjustment (clipping, alignment), fix
and commit now.

- [ ] **Step 5: Merge to main**

```bash
git checkout main
git merge --no-ff feature/sp6c-builtin-buddies \
  -m "Merge feature/sp6c-builtin-buddies: 18 ASCII species + carousel"
```

- [ ] **Step 6: (Optional) Delete local branch**

```bash
git branch -d feature/sp6c-builtin-buddies
```

---

## Verification checklist

After full plan execution, confirm:

- [ ] `pio test -e native` passes 100 % (≥ ~140 tests).
- [ ] `pio run -e seeed_wio_terminal` builds clean.
- [ ] RAM ≤ 60 % (target ~58 % per design).
- [ ] Flash ≤ 60 % (target ~55 % per design).
- [ ] Fresh boot shows axolotl animation on Idle within 1 s.
- [ ] Left / Right on 5-way cycle through ≥ 18 species + any uploads.
- [ ] Overlay `"Name (k/n)"` fades after 1.5 s.
- [ ] Persist restores last-selected buddy after reset.
- [ ] Upload of a character immediately appears in the carousel.
- [ ] Approve / deny / heart / celebrate still animate on the current
      buddy (ASCII species or uploaded GIF).
- [ ] Reserved-name upload attempts are rejected with `ok:false`.
