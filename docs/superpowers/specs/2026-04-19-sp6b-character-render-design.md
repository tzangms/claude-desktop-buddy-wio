# SP6b — Character render via AnimatedGIF

Date: 2026-04-19
Status: design
Predecessor: SP6a (manifest + active-char persist) — merged
Successor: SP6c (built-in fallback buddy)

## Purpose

SP6a landed `manifestActive()` with the parsed character pack in RAM. SP6b
consumes it: render the actual GIF animations on the TFT instead of the
hard-coded ASCII pet. This is the first slice where the user sees their
uploaded buddy on the device.

No additional protocol work. No manifest schema changes. Just decode and
draw.

## Reference

The claude-desktop-buddy project ships a reference firmware for M5StickC
Plus (ESP32) at `anthropics/claude-desktop-buddy/src/character.cpp`. We
port that file's core pattern and adapt to our hardware:

| Axis | Reference (M5StickC Plus) | Wio Terminal (ours) |
|---|---|---|
| MCU | ESP32, ~320 KB SRAM | SAMD51, 192 KB SRAM (70 KB used) |
| FS | LittleFS | Seeed_Arduino_FS + SFUD |
| Display | 135×240 ST7789 via `TFT_eSprite` | 320×240 ILI9341 via `TFT_eSPI` direct |
| BLE | nimBLE | rpcBLE (no impact on decoder) |
| Manifest | parses inline | consumed pre-parsed via `manifestActive()` (SP6a) |

The library `bitbank2/AnimatedGIF@^2.1.1` is platform-agnostic and already
runs on SAMD51.

### What makes the port tractable

The reference assumes **full-frame unoptimized GIFs** from host (commented
in `character.cpp`: `gifsicle --unoptimize --lossy`). Verified against
`anthropics/claude-desktop-buddy/characters/bufo/*.gif` (2026-04-19):

- 96×100 canvas; every sub-image spans the full (0,0)–(96,100).
- 64-color global palette.
- Frame counts: sleep=1, busy=2, idle_0=9, celebrate=27.

Full-frame means we never have to compose partial-rect deltas or obey
GIF disposal semantics. Each frame fully repaints its region.

## Non-goals

- **Built-in fallback buddy.** No character uploaded → ASCII pet stays,
  same as today. Baking a buddy into firmware flash is SP6c.
- **Color theme application.** Manifest colors stay on `manifestActive()`
  for SP6c/future UI theming; this spec hard-codes the current palette.
- **Transcripts / stats relocation beyond what buddy placement needs.**
- **On-device GIF fetch** (`cmd:"list_chars"`, switch-by-name). One
  active char at a time, newest upload wins, set by SP6a.
- **IMU-driven `nap` state.** State machine already has it; wiring is
  out of this spec.

## Layout

Idle screen (320×240) restructured to fit a 96×100 buddy fixed at
`(8, 34)` on the left.

```
+------------------------------------------+
| Claude Buddy           ● connected       |  header 28
+-----+------------------------------------+
|     | Level     Tokens today             |
|     | L7        12.3 kt                  |
|     |                                    |
| buf |  Total  Running  Waiting           |  size-3 digits
| o   |   3       1        2               |  (was size-5)
| 96x |                                    |
| 100 | permissions: /foo                  |
|     | > bash(ls)                         |
|     | > read(package.json)               |
+-----+------------------------------------+
| Hi, tzangms                              |  footer 22
+------------------------------------------+
```

Buddy slot: `x=8, y=34, 96×100`. Stats column starts at `x=112`, reflowed
to live in a 200-wide panel. Big digits drop from size-5 to size-3 to fit.

Other screens (Prompt / Connected / Advertising / FactoryResetConfirm)
are **untouched** in this sprint. The buddy only shows on Idle.

## Component layout

New: `src/character.h`, `src/character.cpp`
Modified: `src/ui.cpp` (renderIdle layout + buddy hook), `platformio.ini`
(add AnimatedGIF lib), `src/config.h` (layout constants).

### `src/character.h`

```cpp
#pragma once
#include <cstddef>
#include "pet.h"   // for PetState enum

// Choose the GIF file for a given state based on the active manifest.
// - Uses the state's own variant when available.
// - Multi-variant idle: rotates sequentially, dwell VARIANT_DWELL_MS (5s)
//   per variant before advancing.
// - If the state has no file: falls back to `sleep`; if sleep missing,
//   falls back to `idle[0]`; if none of those, returns nullptr (caller
//   renders ASCII).
// - nowMs: for variant dwell timing. Pass millis().
const char* characterPickFile(PetState state, uint32_t nowMs);

// Draw one tick of the buddy at (x, y) for the given state. Drives the
// AnimatedGIF decoder; advances a frame when the per-frame delay has
// elapsed. Caller must supply the draw rect.
// Returns true if a frame was actually drawn this tick (for render cache
// bookkeeping in ui.cpp).
bool characterDraw(PetState state, int x, int y, uint32_t nowMs);

// Whether SP6b's renderer is active this session (i.e., manifestActive()
// is non-null AND required files validated at init). If false, caller
// falls back to ASCII pet.
bool characterReady();

// Called once at boot after manifestLoadActiveFromPersist(). Opens each
// state's first-variant file to verify it's a valid GIF. Sets
// characterReady() accordingly. Safe to call on cold boot (no-op if
// manifestActive() is null).
void characterInit();

#ifndef ARDUINO
// Test-only: allow native tests of the pick-file logic without AnimatedGIF.
void _characterResetForTest();
#endif
```

### `src/character.cpp` skeleton

ARDUINO-guarded decoder + public characterReady / characterInit /
characterPickFile / characterDraw. Native fallback stubs let native tests
verify pickFile logic without pulling in AnimatedGIF.

```cpp
#ifdef ARDUINO
#include <AnimatedGIF.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
static AnimatedGIF gif;
static File        gifFile;
static bool        gifOpen = false;
static uint32_t    nextFrameAt = 0;
static bool        ready = false;
// ...file callbacks (open/read/seek/close) wrap SFUD File*
// ...draw callback writes RGB565 scanlines directly to tft via drawPixel
#endif
```

Key decoder pattern (direct from reference, adapted):

1. `gifOpenCb(path, *size)` → open SFUD File, return handle + size.
2. `gifReadCb(handle, buf, len)` → File::read.
3. `gifSeekCb(handle, pos)` → File::seek.
4. `gifCloseCb(handle)` → File::close.
5. `gifDrawCb(GIFDRAW*)` → for each pixel in the scanline, write to
   `tft.drawPixel(x, y, palette[idx])`. Transparent index paints `pal.bg`.

### `src/ui.cpp::renderIdle` changes

Restructure layout constants (can extract to `config.h`). Pet render
branch:

```cpp
if (characterReady()) {
  characterDraw(st, 8, 34, now);
} else {
  // existing ASCII petFace() render (moved to a smaller / pushed-aside slot)
}
```

When `characterReady()` is true, the buddy slot replaces the ASCII block.
When false, fall back to current (small) ASCII area.

Cache keys (`lastPet`, `lastFrame`) stay meaningful — `characterDraw`
returns true on a frame advance so we can invalidate other cached regions
if needed (unlikely; buddy and stats don't overlap).

### State → variant mapping

```cpp
PetState                 → ManifestStateIdx
Sleep                    → MANIFEST_STATE_SLEEP
Idle                     → MANIFEST_STATE_IDLE    (rotates variants)
Busy                     → MANIFEST_STATE_BUSY
Attention                → MANIFEST_STATE_ATTENTION
Celebrate                → MANIFEST_STATE_CELEBRATE
Heart                    → MANIFEST_STATE_HEART
Dizzy                    → MANIFEST_STATE_DIZZY
Nap                      → MANIFEST_STATE_NAP     (fall back to SLEEP)
```

Fall-back chain when a state's variantCount is 0:
`requested → sleep → idle[0] → nullptr (ASCII)`

### Variant rotation (idle only)

State machine per the reference:

```
enter Idle → currentVariant = 0, variantStartedMs = now
play variant[currentVariant].gif through all frames
after last frame: pause animPauseMs = 800
when pause over:
  if (now - variantStartedMs) ≥ VARIANT_DWELL_MS (5000):
    currentVariant = (currentVariant + 1) % variantCount
    variantStartedMs = now
  // otherwise restart the same variant's GIF from frame 0
exit Idle → reset currentVariant = 0 for next Idle entry
```

`characterPickFile(Idle, nowMs)` is therefore **stateful** — it returns
whichever variant is current, and internally advances when the dwell
window has elapsed. Tests seed time via `nowMs` to exercise rotation.

With 9 bufo idle variants at ~1 s playback each, full cycle ≈ 9 × 5 s
= 45 s.

Non-idle states stay on their single file (first variant only — we
don't rotate attention/celebrate/busy/heart/dizzy even if the manifest
lists multiple) and loop until state changes.

## Memory envelope

Per `AnimatedGIF` docs + measurement from reference:

| Component | Size |
|---|---|
| `AnimatedGIF` instance (static) | ~14 KB |
| LZW dict / line buffer (during decode) | ~6 KB |
| Palette + scanline work | ~1 KB |
| **Transient peak during a `gif.playFrame` call** | **~21 KB** |

Current static use: 70 KB (36%). Adding AnimatedGIF instance pushes to
~84 KB (43%). Decoder transient on main-loop stack during `playFrame`
brings working peak to ~105 KB (55%). Plenty of headroom; fits
comfortably. No framebuffer / sprite needed — scanlines go straight to
the display.

## Error handling

| Condition | Behavior |
|---|---|
| `manifestActive()` returns null at boot | `characterReady() == false`; ASCII renders. |
| Required state file missing on flash | Fall-back chain (above). Logged on serial once per state. |
| GIF file won't open | Same fall-back. Logged. |
| Decoder `gif.openFile` fails (not a valid GIF) | Same fall-back. Logged. |
| Decoder error mid-frame (truncated / palette overflow) | Close, mark state as "tried and failed this session", fall-back. |
| Display write during BLE callback stack | Not applicable — renderIdle runs on main loop. |

## Testing

### Native

`test/test_character/test_character.cpp` — new Unity test suite.

Covers the **pick-file logic only** (no GIF decode in native):

1. With no active manifest, `characterReady()` returns false and
   `characterPickFile(*)` returns nullptr.
2. Fresh active manifest: `characterPickFile(Idle, 0)` returns idle[0].
3. After `VARIANT_DWELL_MS`, `characterPickFile(Idle, now)` returns idle[1].
4. After 9 dwells, `characterPickFile(Idle, now)` wraps to idle[0].
5. State without a manifest entry (e.g., Nap on a manifest that only
   lists sleep/idle): falls back to sleep.
6. State without sleep either: falls back to idle[0].
7. Manifest with neither sleep nor idle: returns nullptr.
8. Non-idle state does not advance variant across multiple calls.

Native stub for `characterReady` / `characterInit` lets tests prime the
active manifest via the SP6a test hooks (`_manifestSetActiveFromJson`).

### Device smoke

Runs after Task 8's merge:

- Upload bufo; on idle screen the pet area shows the bufo frog
  animating, not ASCII.
- Issue a heartbeat with `running>0` — buddy switches to `busy.gif`.
- Issue a permission prompt (tool use) — buddy switches to
  `attention.gif` (renderPrompt screen is untouched this sprint, but
  Idle should stay on Attention until resolved if Prompt doesn't take
  over).
- Approve the prompt — ack+heart + manifest `heart.gif` if present.
- Let device sit idle ~45 s; confirm idle variants rotate.
- Upload a DIFFERENT character (or re-upload bufo): screen switches to
  new character without reboot.

## Risks

1. **SFUD read throughput vs GIF frame rate.** A 137 KB celebrate.gif at
   27 frames ≈ 5 KB/frame. SFUD read over QSPI at 104 MHz is fast
   enough (~MB/s class), but confirm no per-call overhead blows the
   budget.
2. **Screen tearing without a sprite.** Per-scanline direct-draw means a
   frame paints top-to-bottom visibly. On a 96×100 region this is
   ~100 × drawPixel ops per scanline = 9600 SPI ops per frame.
   Measured on reference: invisible at ~10 FPS. Worst case we add a
   `TFT_eSprite` 96×100 = ~19 KB framebuffer and `pushSprite` per
   frame. Keep direct-draw as MVP; switch to sprite if visible.
3. **Layout regression.** Shrinking digits from size-5 to size-3 is a
   visual change affecting legibility. Acceptable trade; reversible if
   user objects on smoke.
4. **rpcBLE interaction.** Decoder runs on main loop stack, same path
   that successfully calls SFUD after `fix/xfer-defer-sfud`. No new
   stack concerns.

## Scope estimate

~400 lines `src/character.cpp/h` (ported + adapted) + ~80 lines
`ui.cpp` layout rework + ~30 lines `pet.cpp` hook + ~100 lines native
tests. 1 SP.
