# SP6c — Built-in ASCII buddies + carousel selector

Date: 2026-04-20
Status: design
Predecessors: SP6a (manifest + active-char persist), SP6b (GIF render)

## Purpose

Out-of-box experience: the device ships with 18 hand-drawn ASCII
"species" from the upstream reference (`anthropics/claude-desktop-buddy`)
already on-flash. Users can cycle through them with the 5-way left /
right buttons on the Idle screen. Uploaded GIF characters (SP6a / SP6b)
are added to the same carousel. Persist remembers which is active across
reboots.

Replaces the existing minimal single-species ASCII pet in `src/pet.cpp`.

## Reference

Upstream: `anthropics/claude-desktop-buddy/src/buddies/<name>.cpp` × 18:
`axolotl blob cactus capybara cat chonk dragon duck ghost goose mushroom
octopus owl penguin rabbit robot snail turtle`.

Each species file is ~11 KB and defines 7 state functions (sleep / idle
/ busy / attention / celebrate / dizzy / heart) that paint poses,
sequence via `SEQ[]` lookup tables, and add overlay particles (Z drifting
for sleep, hearts for heart state, sparks for celebrate, …) to a
`TFT_eSprite spr` framebuffer.

Upstream framework:
- `src/buddy.{h,cpp}` — public API (init/tick/invalidate/setSpecies/…),
  species table, per-state dispatch.
- `src/buddy_common.{h,cpp}` — layout constants, color palette, helpers
  (`buddyPrintSprite`, `buddySetCursor`, `buddySetColor`, `buddyPrint`,
  `buddyPrintLine`).

## Non-goals

- **`cmd:"delete_char"`** to remove an uploaded character. Separate SP.
- Running buddy tick on non-Idle screens (Prompt / Ack / etc. stay on
  their existing renders).
- Per-species color theming of the rest of the UI (stats / header).
- Porting the reference's full HUD (info panel, clock, etc.) — those
  live next to the buddy on the M5StickC Plus portrait display and
  don't map to our 320×240 landscape.

## Architecture

```
main::loop (Idle mode):
  buddyTick(petComputeState(appState, now), now)
    │
    ├─ uploaded char active? → character.cpp (GIF path, SP6b)
    └─ built-in species active? → buddies/<name>.cpp fn(t) paints into spr
                                  → spr.pushSprite(BUDDY_X, BUDDY_Y)

main::loop (Idle + ButtonEvent::PressLeft/Right):
  buddyPrev() / buddyNext()
    └─ updates carousel index, persistSetActiveChar(name),
       characterInvalidate() (closes any open GIF),
       triggers 1.5s "Name (k/n)" overlay via renderIdle cache reset
```

Buddy selection is **persist-authoritative**: `activeCharName[33]` in
`PersistData` stores either a reserved species name (`_cat`, `_robot`,
…) or the name of an uploaded char. `buddy.cpp` owns the
species-name ↔ species-index mapping and the upload-dir enumeration.

### Sprite framebuffer

Reference's animations rely on layered overlays (body paints first, then
particles with `setColor + setCursor + print`). Doing that direct to TFT
produces visible flicker between layers. We adopt the reference's
sprite approach: a `TFT_eSprite spr` sized to the buddy slot
(`BUDDY_W × BUDDY_H = 96 × 100 × 2 bytes = 19200 bytes ≈ 19 KB`).

Each `buddyTick` call:
1. `spr.fillSprite(species.bgColor)` clears the sprite.
2. Species state fn paints body + overlays into `spr`.
3. `spr.pushSprite(BUDDY_X, BUDDY_Y)` blits to TFT in one SPI burst.

**Memory envelope:** baseline post-SP6b is 48.3 % RAM (94,996 B). Add
~19 KB sprite → ~58 %. Plus the 18 species files each bring ~500-800 B
of string literals in RO flash (not RAM). Total RAM after SP6c ≈ 58 %;
~80 KB headroom.

### File-system enumeration

`buddyInit` walks `/chars/*` on SFUD (`SFUD.open("/chars").openNextFile()`
loop) at boot to collect uploaded character directory names. Cached
in a module-level `std::vector<std::string> uploadedChars` (max 16
entries; excess ignored).

Carousel order:
```
[ 0 .. 17 ] = built-in species (alphabetical for predictability)
[ 18 .. ] = uploaded chars (dir enumeration order)
```

If a new character uploads via SP4b.4 folder-push transport mid-session,
the enumeration refreshes after `char_end` succeeds (callback hook in
`xferEndChar`). Switching to the new upload is not automatic — user
cycles to it.

## Components

### New: `src/buddy.h`

```cpp
#pragma once
#include <cstdint>

void buddyInit();                  // enumerate /chars, load persisted selection
void buddyTick(uint8_t petState,   // drive the active buddy frame
               uint32_t nowMs);
void buddyInvalidate();            // renderIdle did fullRedraw; next tick repaints
void buddyNext();                  // carousel right
void buddyPrev();                  // carousel left
uint8_t buddySpeciesIdx();         // current index (0..count-1)
uint8_t buddyCount();              // total slots (species + uploads)
const char* buddyCurrentName();    // species slug or upload name

#ifndef ARDUINO
void _buddyResetForTest();
void _buddySetActiveForTest(const char* name);
#endif
```

Internally keeps a `Species[18]` table + the uploadedChars vector, and
a single `currentIdx` int.

### New: `src/buddy_common.h` / `src/buddy_common.cpp`

Port of upstream's helpers adapted to our sprite + TFT_eSPI. Same
exported signatures so species files copy unchanged:

```cpp
extern const int BUDDY_X_CENTER;   // relative to the 96x100 sprite
extern const int BUDDY_Y_BASE;
extern const int BUDDY_Y_OVERLAY;
extern const int BUDDY_CHAR_W;
extern const int BUDDY_CHAR_H;
extern const uint16_t BUDDY_BG, BUDDY_HEART, BUDDY_DIM, BUDDY_YEL,
  BUDDY_WHITE, BUDDY_CYAN, BUDDY_GREEN, BUDDY_PURPLE, BUDDY_RED, BUDDY_BLUE;

void buddyPrintSprite(const char* const* lines, uint8_t nLines,
                      int yOffset, uint16_t color, int xOff = 0);
void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff = 0);
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char* s);
```

Internally writes to `extern TFT_eSprite spr;` declared in
`buddy_common.cpp`.

### New: `src/buddies/*.cpp` × 18

Copy verbatim from upstream. Each file wraps its state functions in a
namespace and exposes them via the `Species` entry in `buddy.cpp`.
Only textual changes required:
- `#include <M5StickCPlus.h>` → **remove** (sprite declared in our
  `buddy_common.cpp`).
- `extern TFT_eSprite spr;` → **remove** (pulled in via
  `buddy_common.h`).

No pose or SEQ edits — the ASCII is the whole point.

### Modified: `src/buttons.{h,cpp}`

Add two new events to the `ButtonEvent` enum: `PressLeft`, `PressRight`.
Extend `pollButtons` to read `WIO_5S_LEFT` and `WIO_5S_RIGHT` with the
same debounce as existing buttons.

### Modified: `src/main.cpp`

- `setup()`: call `buddyInit()` after `characterInit()`.
- `loop()` on Idle: replace the character block with a buddyTick call
  that decides between GIF (uploaded) and sprite (built-in) paths.
- `loop()` button handler: on `PressLeft`/`PressRight`, only react when
  `mode == Mode::Idle`. Call `buddyPrev()`/`buddyNext()`, trigger a
  1.5 s "Name (k/n)" overlay (a new cache field in `renderIdle`).

### Modified: `src/ui.cpp`

- `renderIdle`: drop the ASCII pet branch. The buddy slot is owned by
  `buddyTick` exclusively.
- Add a short-lived overlay at the top of the buddy slot (or footer)
  showing "Name (k/n)" for `BUDDY_OVERLAY_MS = 1500` after a
  carousel switch. Overlay disappears on its own on next fullRedraw.

### Modified: `src/xfer.cpp::xferIsValidName`

Add rejection for names that collide with built-in species. Simplest
form: any name starting with `_` is reserved. The 18 species files use
the species slugs (`cat`, `robot`, etc.) without underscore prefix —
so we **also** reserve exact-match rejection against the 18 slugs
stored in a `kReservedNames[]` table at the top of `buddy.cpp`.

This is a backward-compatible policy tweak: bufo is not affected; no
host currently uploads anything starting with `_` or matching the 18
slugs.

### Modified: `src/pet.{h,cpp}`

**Delete**. The state-machine-only parts (`petTriggerCelebrate`,
`petTriggerHeart`, `petTriggerDizzy`, `petEnterNap`, `petExitNap`,
`petComputeState`) move to a new `src/pet_state.{h,cpp}` (state machine
only, no frames). The old ASCII frame arrays are replaced by the
buddy system.

### Modified: `src/config.h`

Add `BUDDY_OVERLAY_MS = 1500` for the carousel label fade duration.

## Persist schema

**No change.** `activeCharName[33]` already exists (SP6a) and accommodates:
- `""` (empty): fresh device, default to `_cat` or index 0 on first boot.
- `"_<slug>"`: built-in species (reserved name).
- `"<name>"`: uploaded char dir name.

No `PERSIST_VERSION` bump. Existing SP6a persist files Just Work — a
stored `"bufo"` continues to resolve to the uploaded char; a new
reserved species name gets stored on next carousel action.

On `persistInit`, if `activeCharName[0] == '\0'` (never set), the
carousel defaults to index 0 (alphabetically first species, `axolotl`).

## Error handling

| Condition | Behavior |
|---|---|
| `buddyInit` can't open `/chars` dir | uploadedChars stays empty; carousel = 18 species only |
| Persisted `activeCharName` matches no species and no uploaded dir | default to index 0, log warning |
| `buddyNext`/`Prev` with count == 0 | impossible (at least 18 built-ins guaranteed) |
| Uploaded char present at boot but manifest.json invalid | char is still enumerated (selectable), `manifestSetActive` fails, `characterReady()` returns false, carousel remains on the broken name but `buddyTick` detects no species match + no valid GIF → paints an "invalid" placeholder (blank slot + species-BG color). Cycling past moves on normally. |
| More than 16 uploaded chars on flash | excess ignored in enumeration (logged) |

## Testing

### Native

`test/test_buddy/test_buddy.cpp` — new Unity suite.

1. `buddyCount() >= 18` after init with no uploaded chars.
2. `buddySpeciesIdx() == 0` on fresh init.
3. `buddyNext()` advances index; wraps past end.
4. `buddyPrev()` retreats; wraps past 0.
5. `_buddySetActiveForTest("_robot")` → currentIdx matches "_robot".
6. Unknown name default → index 0.
7. Reserved-name check: `xferIsValidName("_cat")` returns false.
8. Reserved-name check: exact match against any of 18 slugs returns false.
9. Reserved-name check: `bufo` still returns true.
10. Cycle through all 18 species → names are unique.

Can't native-test the sprite rendering (TFT_eSprite is ARDUINO-only).
Can't native-test the `/chars` enumeration (SFUD ARDUINO-only). Both
verified on device.

### Device smoke

1. Flash clean firmware → boot shows `axolotl` (idx 0), animated.
2. Right-press: `blob`. Overlay shows `blob (2/18)` for 1.5 s.
3. Left-press: back to `axolotl`.
4. Left-press from idx 0 wraps to `turtle` (last built-in) or last
   uploaded.
5. Upload bufo via Hardware Buddy → after `char_end`, right-press until
   reaching `bufo` → GIF animates.
6. Reset: persist restores `bufo`.
7. Up/down on 5-way: no effect (unused).
8. Left/right during Prompt: no effect (ignored).

## Risks

1. **Port fidelity.** 18 species files contain ~200 KB of hand-tuned
   ASCII. Any silent change during copy (whitespace collapse, escape
   handling on \\, …) breaks pose alignment. Checkout each file byte-for-
   byte from upstream; lint with `diff` against the origin.
2. **Flash budget.** Current 29.5 % → estimated 55-60 % post-SP6c
   (species RO data dominates, code is thin). Monitor during build.
3. **Sprite RAM pressure.** 19 KB is static bss; doesn't bloat over
   time. Peak combined with AnimatedGIF's ~24 KB static + 4 KB transient
   doc = ~47 KB at decode. Still within the 80 KB headroom.
4. **Cross-module hooks at `xferEndChar`.** New: enumeration must
   refresh when a new upload finishes. Implementation uses a direct call
   from `xferEndChar` to `buddyOnNewUpload()` (or re-init). Must not
   block the BLE callback path (SFUD read). xferEndChar already runs on
   the main loop stack per the `fix/xfer-defer-sfud` pattern — safe.
5. **Overlay label timing.** "Name (k/n)" disappearing after 1.5 s
   requires the renderIdle cache to invalidate a specific y-band at the
   overlay expiry. Cleanest implementation: store `overlayUntilMs` in
   appState or renderIdle statics; when expired, clear the overlay
   rect and mark cache invalid so it stays cleared.

## Scope estimate

| Piece | LOC |
|---|-----|
| `buddy_common.{h,cpp}` port | ~200 |
| `buddy.{h,cpp}` core + species table + enumeration + carousel | ~300 |
| `src/buddies/*.cpp` × 18 | ~200 KB source, mostly ASCII data |
| `buttons.{h,cpp}` add PressLeft/Right | ~30 |
| `main.cpp` wiring + carousel button handler | ~80 |
| `ui.cpp` overlay label + ASCII-block removal | ~60 |
| `xfer.cpp` reserved-name check | ~10 |
| `pet.{h,cpp}` split state-machine into `pet_state.*` | ~40 (move) |
| `config.h` constants | ~5 |
| Tests | ~150 |

~1500 hand-written LOC + 200 KB of species ASCII data copied verbatim.
Firmware flash: 29.5 % → ~55 %. RAM: 48.3 % → ~58 %. Flash write budget
fits 512 KB total; RAM fits 192 KB total with >80 KB headroom.

One SP. Mechanical-heavy: the 18 species copies are largely find-and-
verify work, not logic.

## Successor specs

- **SP6d** — `cmd:"delete_char"` protocol support + carousel removes the
  upload on deletion.
- **SP6e** — per-species theming: apply species `bodyColor` to stats
  panel header / labels for extra personality.
