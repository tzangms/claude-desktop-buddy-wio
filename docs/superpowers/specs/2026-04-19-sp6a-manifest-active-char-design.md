# SP6a — Manifest parse + active-char persist

Date: 2026-04-19
Status: design
Successor specs: SP6b (decoder + render swap), SP6c (built-in fallback buddy)

## Purpose

SP4b.4 landed the BLE folder-push transport: `characters/bufo` now reaches
`/chars/bufo/` on QSPI flash. But nothing downstream reads it. The device
still renders the hard-coded ASCII pet from `src/pet.cpp`.

SP6a adds the smallest piece that makes subsequent work possible:

1. On a successful `char_end`, parse the uploaded `manifest.json`.
2. If the manifest validates, mark that character as the **active** one and
   persist the choice.
3. At boot, rehydrate the active manifest into a RAM-resident cache.

No visual change. The `pet.cpp` ASCII renderer keeps running. SP6b will
swap the render source once frames can be decoded.

## Non-goals

- **Decoding or rendering GIFs.** The bytes just sit on flash.
- **Built-in default buddy.** If no character has been uploaded, the
  device renders ASCII exactly as today.
- **Multi-character switching at runtime.** Exactly one active char at a
  time. The newest successful upload wins.
- **`cmd:"delete_char"` or `cmd:"list_chars"`.** Character storage
  lifecycle is SP6b/SP6c territory.
- **Color theme application to non-pet UI.** Parsed, stored, used by SP6b.

## Host-side contract (reference)

Host: `anthropics/claude-desktop-buddy/characters/bufo/manifest.json` (fetched 2026-04-19).

```json
{
  "name": "bufo",
  "colors": {
    "body": "#6B8E23", "bg": "#000000", "text": "#FFFFFF",
    "textDim": "#808080", "ink": "#000000"
  },
  "states": {
    "sleep": "sleep.gif",
    "idle":  ["idle_0.gif", "idle_1.gif", ..., "idle_8.gif"],
    "busy":       "busy.gif",
    "attention":  "attention.gif",
    "celebrate":  "celebrate.gif",
    "dizzy":      "dizzy.gif",
    "heart":      "heart.gif"
  }
}
```

Notes the firmware must accommodate:

- Each state's value is **either** a string **or** an array of strings.
- `idle` in bufo has 9 variants. Cap at 16 in firmware with room to grow.
- Host has no `nap` state. Firmware falls back to `sleep` when SP6b
  renders and the active state is `Nap` but the manifest lacks it.
- Colors are hex `#RRGGBB`; display wants RGB565.

## Firmware representation

`src/manifest.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>

static constexpr size_t MANIFEST_MAX_VARIANTS     = 16;
static constexpr size_t MANIFEST_FILENAME_MAX     = 32;
static constexpr size_t MANIFEST_NAME_MAX         = 32;
// Enum order matches pet.h::PetState.
static constexpr size_t MANIFEST_STATE_COUNT      = 8;

struct CharManifest {
  char     name[MANIFEST_NAME_MAX + 1];
  uint16_t colorBody;     // RGB565, converted from "#RRGGBB"
  uint16_t colorBg;
  uint16_t colorText;
  uint16_t colorTextDim;
  uint16_t colorInk;
  uint8_t  stateVariantCount[MANIFEST_STATE_COUNT];
  char     states[MANIFEST_STATE_COUNT]
                 [MANIFEST_MAX_VARIANTS]
                 [MANIFEST_FILENAME_MAX + 1];
};
```

Approximate footprint: `33 + 10 + 8 + 8 * 16 * 33` ≈ `4.3 KB` bss on the
cached singleton. Acceptable at 33.7% / 192 KB RAM. Stored once, not per
tick.

API:

```cpp
// Parse manifest.json at `path` (absolute, e.g. "/chars/bufo/manifest.json").
// On success, fill out, return true. On failure, set err and return false.
bool manifestParseFile(const char* path, CharManifest& out, std::string& err);

// Load manifest for `charName` from "/chars/{charName}/manifest.json" and
// cache it as the active manifest. Returns false on parse error (active
// manifest is unchanged).
bool manifestSetActive(const char* charName);

// Currently cached active manifest, or nullptr if none.
const CharManifest* manifestActive();

// Test-only reset.
#ifndef ARDUINO
void _manifestResetForTest();
#endif
```

Implementation: `ArduinoJson` with `DynamicJsonDocument` sized to 4 KB (the
manifest plus some slack; bufo manifest is 542 bytes). Parsing walks each
state key; if the value is a string we store one variant, if an array we
iterate up to `MANIFEST_MAX_VARIANTS` and set a warning in `err` on
overflow (we still return true so a fat manifest doesn't brick upload).

State name → index mapping is a small static table keyed on the
`PetState` enum order (`sleep`, `idle`, `busy`, `attention`, `celebrate`,
`heart`, `dizzy`, `nap`).

Hex color → RGB565: standard 5:6:5 bit-pack.

## Touchpoints

### `src/xfer.cpp::xferEndChar()`

Extends the current (tiny) implementation:

```cpp
bool xferEndChar() {
  if (state != State::CharOpen) return false;
  state = State::Idle;
  // charName is still latched here. Try the manifest.
  char manifestPath[96];
  std::snprintf(manifestPath, sizeof(manifestPath),
                "/chars/%s/manifest.json", charName);
  std::string err;
  CharManifest m;
  if (!manifestParseFile(manifestPath, m, err)) {
    // Upload hit flash but isn't a valid character pack.
    // Leave prior active char untouched. Host sees ack ok:false.
    return false;
  }
  if (!manifestSetActive(charName)) return false;   // cache & validate
  persistSetActiveChar(charName);                    // async flush via persistTick
  return true;
}
```

The existing BLE callback path already defers SFUD writes via the queue
added in `fix/xfer-defer-sfud`, so running the manifest parse here is
safe — we're in `xferTick()` on the main loop stack.

### `src/persist.cpp` / `persist.h`

`PersistData` gains one field:

```cpp
struct PersistData {
  uint32_t magic;
  uint32_t version;
  ...existing fields...
  char     activeCharName[33];   // "" = no active char
};

static constexpr uint32_t PERSIST_VERSION = 2;  // bump from 1
```

New accessors:

```cpp
void persistSetActiveChar(const char* name);
const char* persistGetActiveChar();   // "" if none
```

Migration: bump-version-and-reset (per decision 2026-04-19). Existing
device boots with `setDefaults()`, losing lvl / tokens / appr / deny
counters. Documented in commit message. No silent data preservation logic.

### `src/main.cpp::setup()`

After `persistInit()`:

```cpp
const char* active = persistGetActiveChar();
if (active[0] != '\0') {
  manifestSetActive(active);   // best-effort; silent on failure
}
```

If the on-disk manifest has since been corrupted or deleted out of band,
`manifestSetActive` returns false and `manifestActive()` stays `nullptr`.
SP6b renderers will treat that as "no active char, use ASCII".

## Error handling

| Condition                                | Behavior                                    |
|------------------------------------------|---------------------------------------------|
| `char_end` arrives with no manifest.json | `ack ok:false`, prior active char intact    |
| manifest.json present but invalid JSON   | `ack ok:false`, prior active char intact    |
| Required field (`name` or `colors`) missing | `ack ok:false`, prior active char intact |
| State value array longer than 16 variants | truncate, continue, set warning in err    |
| State name not in firmware enum           | ignore that entry, continue                 |
| Upload replaces an already-active char    | new one becomes active (newest wins)        |
| Manifest parse succeeds, flash write of activeCharName fails | `ack ok:false`, active cache reverted |

Strict rejection (rows 1–3) is intentional — if the bytes on flash aren't
a coherent character, the user's intent wasn't met and reporting failure
gives the host a chance to retry. This matches how `file_end` rejects
size mismatches today.

## Testing

`test/test_manifest/test_manifest.cpp` — new Unity test suite.

Required cases:

1. **Happy path** — feed the exact bufo manifest JSON, assert name,
   colors converted to RGB565, each state's variant count and filenames.
2. **String-valued state** — single-file `sleep` parses to variant count 1.
3. **Array-valued state** — `idle: ["idle_0.gif", "idle_1.gif"]` parses
   to variant count 2.
4. **Missing optional state** — manifest without `celebrate` → count 0.
5. **Missing required `name`** → returns false, err non-empty.
6. **Missing required `colors`** → returns false, err non-empty.
7. **Malformed JSON** → returns false.
8. **Array over cap** — 20-element `idle` array → truncates to 16,
   returns true, err contains `"truncated"`.
9. **Unknown state name** (e.g. `"dancing"`) → ignored, no error.
10. **Hex color parsing** — `#6B8E23` → correct RGB565.
11. **`manifestSetActive` / `manifestActive` roundtrip** — set, then get
    returns cached manifest with matching name.
12. **`manifestSetActive` failure** — bad path leaves prior active
    manifest unchanged.

`test/test_persist` — update:

- Version bump is a behavior change. Add a test that verifies loading a
  v1-sized blob falls through to `setDefaults()`.
- Add a test that `persistSetActiveChar("bufo")` + `persistTick` +
  re-init round-trips the name.

`test/test_xfer` — extend:

- After a full char roundtrip that writes a valid manifest, `xferEndChar`
  returns true and `manifestActive()->name == "bufo"`.
- After a roundtrip without manifest.json, `xferEndChar` returns false
  and `manifestActive()` is unchanged.

## Risks

1. **ArduinoJson 4 KB doc.** `protocol.cpp` uses `StaticJsonDocument<2048>`
   (stack-allocated). 4 KB on the main-loop stack is still safe, but
   doubling the existing budget warrants a one-time stack check. If in
   doubt, switch the manifest doc to `DynamicJsonDocument` (heap) — the
   parse runs once per upload, not per tick, so malloc cost is
   negligible.
2. **Existing user stats wiped on first boot.** Accepted tradeoff (per
   2026-04-19 decision). Commit message should call this out clearly.
3. **Flash read at boot adds latency.** One `SFUD.open` + a ~500-byte
   read + ~1 ms JSON parse. Invisible next to the existing BLE init
   wait. Low risk.
4. **Manifest cache is ~4.3 KB bss.** Fits, but note it's one of the
   larger single-object allocations in the project. Document in the
   header.

## Dependencies on / handoffs to later specs

- **SP6b (decoder + render swap)** consumes `manifestActive()` to pick
  which file to decode for a given `PetState`. Requires this spec to
  land.
- **SP6c (built-in fallback)** fills `manifestActive()` from a
  compiled-in asset when flash is empty. Orthogonal to this spec: the
  cache interface is the same, the source is different.

## Estimated scope

~250 lines across `manifest.cpp/h` + minor edits in `xfer.cpp`,
`persist.cpp`, `main.cpp`. ~12 new tests. One session.
