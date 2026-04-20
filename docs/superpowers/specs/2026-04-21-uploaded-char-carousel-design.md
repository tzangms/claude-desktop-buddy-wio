# Uploaded Char Carousel Design

## Goal

Let the user cycle through uploaded characters with the 5-way joystick's left/right directions. Each switch updates the active char, persists it, and briefly shows the char name over the buddy region. Scope is intentionally narrow: **no built-in species, no pet.cpp refactor, no animations**. This is a redo of SP6c with the carousel piece only.

## Non-Goals

- Built-in ASCII buddies / species registry (deferred indefinitely)
- pet.cpp extraction / pet_state split
- Overlay fade-in/out animation
- Showing char previews in a menu UI
- Deleting chars from the device

## User Experience

1. User has ≥ 2 uploaded chars in `/chars/` (e.g. `bufo`, `cat-variant`).
2. On the Idle screen, user pushes 5-way joystick left or right.
3. Active char switches; new GIF begins playing in the buddy slot immediately.
4. A 1.5-second overlay shows the new char's name above the buddy area.
5. Selection is persisted; next boot restores the last-selected char.

Edge cases:
- **0 uploaded chars**: press does nothing (no overlay, no state change).
- **1 uploaded char**: press does not change active char, but the overlay still appears, showing the current char name (gives feedback that the button was registered).
- **Upload mid-session**: newly uploaded char is picked up the next time left/right is pressed (because enumeration runs fresh on every press).
- **Persisted char missing from flash**: treated as "not found in list" → carousel starts at index 0 on first press.
- **Press outside Idle**: ignored by main.cpp dispatch; other modes reuse A/B/C as normal.

## Architecture

Three changes, no heavy refactor:

### 1. New `src/carousel.cpp` + `src/carousel.h`

Public API:

```cpp
// Enumerate /chars/ subdirectories into `out` (alphabetical, capped at `max`).
// Returns count written. Native-testable pure logic (platform layer mocked).
size_t carouselEnumerate(char out[][33], size_t max);

// Main entry point. Called from main.cpp on PressLeft/PressRight.
// - If 0 chars: no-op, returns false (caller skips render).
// - If 1 char: set overlay with current name, return true.
// - If >= 2 chars: advance, persist, swap manifest, refresh character
//   module, set overlay, return true.
bool carouselAdvance(AppState& s, bool forward, uint32_t nowMs);
```

Internal helpers:
- Find current char's index in the enumerated list (strcmp with `persistGetActiveChar()`); missing → idx = 0.
- Wrap with `(idx + n ± 1) % n`.

### 2. `src/buttons.{h,cpp}` — two new events

```cpp
enum class ButtonEvent : uint8_t {
  None, PressA, PressC, LongPressNav,
  PressLeft,   // NEW
  PressRight,  // NEW
};
```

Debounce table grows by two rows for `WIO_5S_LEFT` and `WIO_5S_RIGHT`. Existing `pollButtons` logic unchanged.

### 3. Overlay state in `AppState` + `renderIdle`

`state.h`:
```cpp
uint32_t buddyOverlayUntilMs = 0;
char     buddyOverlayName[MANIFEST_NAME_MAX + 1] = {0};
```

`ui.cpp::renderIdle`: if `now < s.buddyOverlayUntilMs`, paint background rect + name text in the strip `(BUDDY_X, BUDDY_Y - 12, BUDDY_W, 10)`. If not, leave that strip alone (GIF paints normally below).

`main.cpp`: after `carouselAdvance` sets overlay, `render(true)` so the overlay appears right away. On overlay expiry, set `pendingRender = true` so the strip gets repainted clean.

### 4. `characterRefreshManifest()` in `character.{h,cpp}`

New public API:
```cpp
// Caller already swapped manifestActive() to a new manifest. Close any
// open GIF, reset internal state, re-validate readiness against the new
// manifest. Next characterTick() reopens a file for the current state.
void characterRefreshManifest();
```

Implementation: close decoder if open, clear `s_ready`, clear internal "current state", then re-run the validation block from `characterInit()`. Does NOT paint anything — just resets the module so the main loop's existing tick logic does the reopening.

Main-loop static `lastCharState` / `lastCharInit`: we rely on `characterInvalidate()` (already called by `renderIdle` on fullRedraw) to force reopen. The `pendingRender = true` after carousel advance → fullRedraw → characterInvalidate → next tick reopens with new manifest.

## Data Flow

```
ButtonEvent::PressLeft (in Idle)
  └─> carouselAdvance(s, forward=false, now)
        ├─ n = carouselEnumerate(names)
        ├─ if n == 0: return (no-op)
        ├─ idx = find(names, persistGetActiveChar())  // -1 → 0
        ├─ if n >= 2:
        │     newIdx = (idx + n - 1) % n  (or idx+1 for forward)
        │     persistSetActiveChar(names[newIdx])
        │     manifestSetActive(names[newIdx])
        │     characterRefreshManifest()
        ├─ strncpy(s.buddyOverlayName, names[active])
        └─ s.buddyOverlayUntilMs = now + BUDDY_OVERLAY_MS
  └─> render(true)
        └─ renderIdle fullRedraw
              ├─ characterInvalidate() (existing behaviour)
              └─ if overlay active: paint name strip

Later: main.cpp tracks prev tick's `overlayWasActive` boolean.
  When `overlayWasActive && now >= s.buddyOverlayUntilMs`:
    pendingRender = true  (repaint strip, GIF shows through)
    overlayWasActive = false
```

## Files Touched

**New:**
- `src/carousel.h`
- `src/carousel.cpp`
- `test/test_carousel.cpp`

**Modified:**
- `src/buttons.h` — `PressLeft`, `PressRight` enum values
- `src/buttons.cpp` — `WIO_5S_LEFT`, `WIO_5S_RIGHT` in `btns[]`
- `src/state.h` — `buddyOverlayUntilMs`, `buddyOverlayName[]`
- `src/character.h` — `characterRefreshManifest` declaration
- `src/character.cpp` — `characterRefreshManifest` implementation
- `src/ui.cpp::renderIdle` — overlay strip paint
- `src/main.cpp` — button dispatch + overlay-expiry detection
- `src/config.h` — `BUDDY_OVERLAY_MS = 1500`
- `platformio.ini` — native `build_src_filter` adds `+<carousel.cpp>`
- `test/test_main.cpp` — register carousel suite

## Testing

**Native (Unity + ArduinoFake):**
- `carouselEnumerate`: alphabetical order, truncation at cap
- `carouselAdvance`: 0 chars returns early, no state change
- `carouselAdvance`: 1 char skips persist/manifest, still sets overlay
- `carouselAdvance`: ≥ 2 chars forward wraps around end → start
- `carouselAdvance`: ≥ 2 chars backward wraps around start → end
- `carouselAdvance`: active char not in list → starts at idx 0
- `carouselAdvance`: overlay name and deadline set correctly

**Device smoke:**
- Boot with ≥ 2 chars uploaded → confirm last-selected char plays
- Push left → GIF swaps + overlay name appears for ~1.5s
- Push right from same state → GIF wraps in opposite direction
- Upload new char via BLE → push left/right → new char appears in rotation
- Reset with only 1 char → push left → overlay shows name, GIF unchanged
- Reset after wiping `/chars/` → push left → nothing happens, no crash
- Factory reset → reboot → no crash, default char (or none) handled gracefully

## Out of Scope (Explicit)

These are intentionally left for later or never:
- Built-in species / ASCII buddies
- pet.cpp → pet_state.cpp refactor
- Overlay animations
- Carousel menu UI
- Deleting chars from device
- Visual indicator of position (e.g. "2/5" in carousel)

The entire previous SP6c branch (with 18 species + pet.cpp removal + sprite framebuffer) is abandoned. This design intentionally reuses only the carousel idea from SP6c and implements it in isolation against the known-good SP6b baseline.
