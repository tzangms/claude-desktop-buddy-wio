# SP6a — Manifest parse + active-char persist Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse uploaded `manifest.json` on `char_end`, persist the active character name across reboots, rehydrate a RAM-cached `CharManifest` at boot. No visual change.

**Architecture:** A new `manifest.cpp/h` module does JSON → `CharManifest` with ArduinoJson, exposes a module-level cached "active" manifest, and a flash reader. `xferEndChar` calls the parser when a char finishes uploading; `persist` adds one `char activeCharName[33]` field and bumps `PERSIST_VERSION` 1→2 (stats reset accepted). `main::setup` rehydrates the cache from flash after `persistInit`.

**Tech Stack:** C++17, Arduino framework, PlatformIO, ArduinoJson 6, Unity test. Native env covers manifest parse via a pure `manifestParseJson(const char*, size_t)`; ARDUINO-only glue wires `manifestParseFile` into `xferEndChar`.

**Design spec:** `docs/superpowers/specs/2026-04-19-sp6a-manifest-active-char-design.md`

**Branch:** `feature/sp6a-manifest-active-char` off `main`. Merge back via `--no-ff` when done (mirror SP4b.4 / persist fix pattern).

**File structure (summary):**

| File | Responsibility |
|---|---|
| `src/manifest.h` (new) | `CharManifest` struct, state indices, API decls |
| `src/manifest.cpp` (new) | `manifestParseJson` / `manifestParseFile` / active cache |
| `test/test_manifest/test_manifest.cpp` (new) | 12 Unity test cases |
| `src/persist.h` / `persist.cpp` | `activeCharName[33]`, version bump, getter/setter |
| `src/xfer.cpp::xferEndChar` | post-success manifest parse (ARDUINO-only) |
| `src/main.cpp::setup` | post-persistInit rehydrate (ARDUINO-only) |
| `platformio.ini` | add `manifest.cpp` to native `build_src_filter` |

---

### Task 1: Branch + skeleton module + build wiring

**Files:**
- Create: `src/manifest.h`
- Create: `src/manifest.cpp`
- Create: `test/test_manifest/test_manifest.cpp`
- Modify: `platformio.ini`

- [ ] **Step 1: Create feature branch**

```bash
git checkout main
git checkout -b feature/sp6a-manifest-active-char
```

- [ ] **Step 2: Write `src/manifest.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Sized for bufo (9 idle variants). Raise if a real manifest needs more.
static constexpr size_t MANIFEST_MAX_VARIANTS  = 16;
static constexpr size_t MANIFEST_FILENAME_MAX  = 32;
static constexpr size_t MANIFEST_NAME_MAX      = 32;

// Indices match src/pet.h::PetState enum ordering. Hard-coded so tests
// and manifest.cpp agree without pulling pet.h into manifest.h.
enum ManifestStateIdx : uint8_t {
  MANIFEST_STATE_SLEEP = 0,
  MANIFEST_STATE_IDLE,
  MANIFEST_STATE_BUSY,
  MANIFEST_STATE_ATTENTION,
  MANIFEST_STATE_CELEBRATE,
  MANIFEST_STATE_HEART,
  MANIFEST_STATE_DIZZY,
  MANIFEST_STATE_NAP,
  MANIFEST_STATE_COUNT,
};

struct CharManifest {
  char     name[MANIFEST_NAME_MAX + 1];
  uint16_t colorBody;     // RGB565
  uint16_t colorBg;
  uint16_t colorText;
  uint16_t colorTextDim;
  uint16_t colorInk;
  uint8_t  stateVariantCount[MANIFEST_STATE_COUNT];
  char     states[MANIFEST_STATE_COUNT]
                 [MANIFEST_MAX_VARIANTS]
                 [MANIFEST_FILENAME_MAX + 1];
};

// Parse a JSON blob into `out`. Returns true on success; sets `err` on
// failure or non-fatal warning (e.g. variant array truncation still
// returns true with err set).
bool manifestParseJson(const char* json, size_t len,
                       CharManifest& out, std::string& err);

// Read "/chars/{charName}/manifest.json" from SFUD, parse, cache as
// active. On failure, active cache is unchanged.
bool manifestSetActive(const char* charName);

// Currently cached manifest, or nullptr if none.
const CharManifest* manifestActive();

#ifndef ARDUINO
// Test-only hooks.
void _manifestResetForTest();
bool _manifestSetActiveFromJson(const char* json, size_t len);
#endif
```

- [ ] **Step 3: Write `src/manifest.cpp` stub (all functions return false / nullptr)**

```cpp
#include "manifest.h"

#include <cstring>

namespace {
  bool hasActive = false;
  CharManifest active;
}

bool manifestParseJson(const char*, size_t, CharManifest&, std::string& err) {
  err = "not implemented";
  return false;
}

bool manifestSetActive(const char*) { return false; }

const CharManifest* manifestActive() {
  return hasActive ? &active : nullptr;
}

#ifndef ARDUINO
void _manifestResetForTest() {
  hasActive = false;
  std::memset(&active, 0, sizeof(active));
}
bool _manifestSetActiveFromJson(const char* json, size_t len) {
  std::string err;
  if (!manifestParseJson(json, len, active, err)) return false;
  hasActive = true;
  return true;
}
#endif
```

- [ ] **Step 4: Write `test/test_manifest/test_manifest.cpp` with one trivial passing test**

```cpp
#include <unity.h>
#include "manifest.h"

void test_active_initially_null() {
  _manifestResetForTest();
  TEST_ASSERT_NULL(manifestActive());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_active_initially_null);
  return UNITY_END();
}
```

- [ ] **Step 5: Add `manifest.cpp` to native `build_src_filter`**

In `platformio.ini`, update the `[env:native]` section:

```
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp> +<pet.cpp> +<xfer.cpp> +<manifest.cpp>
```

- [ ] **Step 6: Run the new test, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: `test_active_initially_null [PASSED]`.

- [ ] **Step 7: Commit**

```bash
git add src/manifest.h src/manifest.cpp test/test_manifest/test_manifest.cpp platformio.ini
git commit -m "sp6a: skeleton manifest module + test harness"
```

---

### Task 2: Hex `#RRGGBB` → RGB565

**Files:**
- Modify: `src/manifest.cpp`
- Modify: `test/test_manifest/test_manifest.cpp`

- [ ] **Step 1: Add failing tests**

In `test_manifest.cpp`, before `main`, add:

```cpp
// Expose the helper for testing — declared extern in manifest.cpp.
extern uint16_t _manifestHex24ToRgb565(const char* hex);

void test_hex_to_rgb565_black() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565("#000000"));
}
void test_hex_to_rgb565_white() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, _manifestHex24ToRgb565("#FFFFFF"));
}
void test_hex_to_rgb565_bufo_body() {
  // #6B8E23 → R=0x6B (5 bits: 0x0D), G=0x8E (6 bits: 0x23), B=0x23 (5 bits: 0x04)
  // (0x0D<<11) | (0x23<<5) | 0x04 = 0x6C64.
  TEST_ASSERT_EQUAL_HEX16(0x6C64, _manifestHex24ToRgb565("#6B8E23"));
}
void test_hex_to_rgb565_bad_returns_zero() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565("notahex"));
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565(nullptr));
}
```

Register them in `main`:

```cpp
RUN_TEST(test_hex_to_rgb565_black);
RUN_TEST(test_hex_to_rgb565_white);
RUN_TEST(test_hex_to_rgb565_bufo_body);
RUN_TEST(test_hex_to_rgb565_bad_returns_zero);
```

- [ ] **Step 2: Run tests, verify linker errors (helper not defined)**

```bash
pio test -e native -f test_manifest
```

Expected: undefined reference to `_manifestHex24ToRgb565`.

- [ ] **Step 3: Implement helper in `src/manifest.cpp`**

At file top after includes:

```cpp
namespace {
  int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool parseHex24(const char* hex, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!hex) return false;
    if (hex[0] != '#') return false;
    for (int i = 1; i < 7; ++i) if (hexDigit(hex[i]) < 0) return false;
    if (hex[7] != '\0') return false;
    r = (hexDigit(hex[1]) << 4) | hexDigit(hex[2]);
    g = (hexDigit(hex[3]) << 4) | hexDigit(hex[4]);
    b = (hexDigit(hex[5]) << 4) | hexDigit(hex[6]);
    return true;
  }

  uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) |
           ((uint16_t)(g & 0xFC) << 3) |
           ((uint16_t)(b & 0xF8) >> 3);
  }
}

uint16_t _manifestHex24ToRgb565(const char* hex) {
  uint8_t r, g, b;
  if (!parseHex24(hex, r, g, b)) return 0;
  return rgb565(r, g, b);
}
```

- [ ] **Step 4: Run tests, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/manifest.cpp test/test_manifest/test_manifest.cpp
git commit -m "sp6a: hex24→RGB565 helper"
```

---

### Task 3: Parse `name` + `colors` (happy path + required-field rejection)

**Files:**
- Modify: `src/manifest.cpp`
- Modify: `test/test_manifest/test_manifest.cpp`

- [ ] **Step 1: Add failing tests**

```cpp
static const char* kBufoMinimal = R"({
  "name": "bufo",
  "colors": {
    "body": "#6B8E23", "bg": "#000000", "text": "#FFFFFF",
    "textDim": "#808080", "ink": "#000000"
  },
  "states": {}
})";

void test_parse_name_and_colors() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoMinimal, std::strlen(kBufoMinimal),
                                     m, err));
  TEST_ASSERT_EQUAL_STRING("bufo", m.name);
  TEST_ASSERT_EQUAL_HEX16(0x6C64, m.colorBody);
  TEST_ASSERT_EQUAL_HEX16(0x0000, m.colorBg);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, m.colorText);
  TEST_ASSERT_EQUAL_HEX16(0x8410, m.colorTextDim);  // #808080
  TEST_ASSERT_EQUAL_HEX16(0x0000, m.colorInk);
}

void test_parse_missing_name_rejects() {
  const char* j = R"({"colors":{"body":"#000000","bg":"#000000",
    "text":"#FFFFFF","textDim":"#808080","ink":"#000000"},"states":{}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_TRUE(err.find("name") != std::string::npos);
}

void test_parse_missing_colors_rejects() {
  const char* j = R"({"name":"bufo","states":{}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_TRUE(err.find("colors") != std::string::npos);
}

void test_parse_malformed_json_rejects() {
  const char* j = "{not json";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
}
```

Register:

```cpp
RUN_TEST(test_parse_name_and_colors);
RUN_TEST(test_parse_missing_name_rejects);
RUN_TEST(test_parse_missing_colors_rejects);
RUN_TEST(test_parse_malformed_json_rejects);
```

Also add `#include <cstring>` and `#include <ArduinoJson.h>` in test file top if not already there (ArduinoJson is header-only, already in `lib_deps` for native).

- [ ] **Step 2: Run tests, verify failures**

```bash
pio test -e native -f test_manifest
```

Expected: 4 new tests FAIL ("not implemented" or assertion failures).

- [ ] **Step 3: Implement name + colors parse**

Replace the stub `manifestParseJson` in `src/manifest.cpp`:

```cpp
#include <ArduinoJson.h>

namespace {
  bool readRequiredColor(JsonObjectConst colors, const char* key,
                         uint16_t& out, std::string& err) {
    if (!colors.containsKey(key)) {
      err = std::string("colors.") + key + " missing";
      return false;
    }
    const char* hex = colors[key] | "";
    uint8_t r, g, b;
    if (!parseHex24(hex, r, g, b)) {
      err = std::string("colors.") + key + " not #RRGGBB";
      return false;
    }
    out = rgb565(r, g, b);
    return true;
  }
}

bool manifestParseJson(const char* json, size_t len,
                       CharManifest& out, std::string& err) {
  err.clear();
  std::memset(&out, 0, sizeof(out));

  DynamicJsonDocument doc(4096);
  DeserializationError de = deserializeJson(doc, json, len);
  if (de) { err = de.c_str(); return false; }

  JsonObjectConst root = doc.as<JsonObjectConst>();

  const char* name = root["name"] | "";
  if (name[0] == '\0') { err = "name missing"; return false; }
  std::strncpy(out.name, name, MANIFEST_NAME_MAX);
  out.name[MANIFEST_NAME_MAX] = '\0';

  if (!root.containsKey("colors") || !root["colors"].is<JsonObjectConst>()) {
    err = "colors missing"; return false;
  }
  JsonObjectConst colors = root["colors"].as<JsonObjectConst>();
  if (!readRequiredColor(colors, "body",    out.colorBody,    err)) return false;
  if (!readRequiredColor(colors, "bg",      out.colorBg,      err)) return false;
  if (!readRequiredColor(colors, "text",    out.colorText,    err)) return false;
  if (!readRequiredColor(colors, "textDim", out.colorTextDim, err)) return false;
  if (!readRequiredColor(colors, "ink",     out.colorInk,     err)) return false;

  // states parsed in Task 4.
  return true;
}
```

- [ ] **Step 4: Run tests, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: 9 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/manifest.cpp test/test_manifest/test_manifest.cpp
git commit -m "sp6a: parse name + colors with required-field rejection"
```

---

### Task 4: Parse `states` — string, array, and truncation

**Files:**
- Modify: `src/manifest.cpp`
- Modify: `test/test_manifest/test_manifest.cpp`

- [ ] **Step 1: Add failing tests**

```cpp
static const char* kBufoFull = R"({
  "name": "bufo",
  "colors": { "body":"#6B8E23","bg":"#000000","text":"#FFFFFF",
              "textDim":"#808080","ink":"#000000" },
  "states": {
    "sleep": "sleep.gif",
    "idle":  ["idle_0.gif","idle_1.gif","idle_2.gif"],
    "busy":      "busy.gif",
    "attention": "attention.gif",
    "celebrate": "celebrate.gif",
    "dizzy":     "dizzy.gif",
    "heart":     "heart.gif"
  }
})";

void test_parse_state_string_single_variant() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoFull, std::strlen(kBufoFull),
                                     m, err));
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_SLEEP]);
  TEST_ASSERT_EQUAL_STRING("sleep.gif", m.states[MANIFEST_STATE_SLEEP][0]);
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_BUSY]);
  TEST_ASSERT_EQUAL_STRING("busy.gif", m.states[MANIFEST_STATE_BUSY][0]);
}

void test_parse_state_array_multiple_variants() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoFull, std::strlen(kBufoFull),
                                     m, err));
  TEST_ASSERT_EQUAL_UINT8(3, m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif", m.states[MANIFEST_STATE_IDLE][0]);
  TEST_ASSERT_EQUAL_STRING("idle_1.gif", m.states[MANIFEST_STATE_IDLE][1]);
  TEST_ASSERT_EQUAL_STRING("idle_2.gif", m.states[MANIFEST_STATE_IDLE][2]);
}

void test_parse_state_missing_is_zero_count() {
  // kBufoFull has no "nap"
  CharManifest m{};
  std::string err;
  manifestParseJson(kBufoFull, std::strlen(kBufoFull), m, err);
  TEST_ASSERT_EQUAL_UINT8(0, m.stateVariantCount[MANIFEST_STATE_NAP]);
}

void test_parse_state_array_over_cap_truncates_with_warning() {
  // 20-element array
  std::string j = R"({"name":"x","colors":{"body":"#000000","bg":"#000000",
    "text":"#000000","textDim":"#000000","ink":"#000000"},
    "states":{"idle":[)";
  for (int i = 0; i < 20; ++i) {
    if (i) j += ",";
    j += "\"f" + std::to_string(i) + ".gif\"";
  }
  j += "]}}";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(j.c_str(), j.size(), m, err));
  TEST_ASSERT_EQUAL_UINT8(MANIFEST_MAX_VARIANTS,
                          m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_TRUE(err.find("truncated") != std::string::npos);
}
```

Register all four in `main`.

- [ ] **Step 2: Run tests, verify failures**

```bash
pio test -e native -f test_manifest
```

Expected: 4 new tests FAIL (variant counts are 0, filenames empty).

- [ ] **Step 3: Implement states parse**

In `src/manifest.cpp`, add (before `manifestParseJson`):

```cpp
namespace {
  struct StateName { const char* key; ManifestStateIdx idx; };
  constexpr StateName kStateNames[] = {
    {"sleep",     MANIFEST_STATE_SLEEP},
    {"idle",      MANIFEST_STATE_IDLE},
    {"busy",      MANIFEST_STATE_BUSY},
    {"attention", MANIFEST_STATE_ATTENTION},
    {"celebrate", MANIFEST_STATE_CELEBRATE},
    {"heart",     MANIFEST_STATE_HEART},
    {"dizzy",     MANIFEST_STATE_DIZZY},
    {"nap",       MANIFEST_STATE_NAP},
  };

  void storeVariant(CharManifest& out, ManifestStateIdx idx, const char* fn) {
    uint8_t& n = out.stateVariantCount[idx];
    if (n >= MANIFEST_MAX_VARIANTS) return;
    std::strncpy(out.states[idx][n], fn, MANIFEST_FILENAME_MAX);
    out.states[idx][n][MANIFEST_FILENAME_MAX] = '\0';
    ++n;
  }

  void parseStates(JsonObjectConst states, CharManifest& out, std::string& err) {
    for (const auto& sn : kStateNames) {
      if (!states.containsKey(sn.key)) continue;
      JsonVariantConst v = states[sn.key];
      if (v.is<const char*>()) {
        storeVariant(out, sn.idx, v.as<const char*>());
      } else if (v.is<JsonArrayConst>()) {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        size_t seen = 0;
        for (JsonVariantConst f : arr) {
          const char* fn = f | "";
          if (fn[0] == '\0') continue;
          if (out.stateVariantCount[sn.idx] < MANIFEST_MAX_VARIANTS) {
            storeVariant(out, sn.idx, fn);
          }
          ++seen;
        }
        if (seen > MANIFEST_MAX_VARIANTS && err.empty()) {
          err = std::string("states.") + sn.key + " truncated";
        }
      }
      // Unknown types ignored silently.
    }
  }
}
```

Then inside `manifestParseJson`, replace the `// states parsed in Task 4.` comment with:

```cpp
  if (root.containsKey("states") && root["states"].is<JsonObjectConst>()) {
    parseStates(root["states"].as<JsonObjectConst>(), out, err);
  }
  return true;
```

- [ ] **Step 4: Run tests, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: 13 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/manifest.cpp test/test_manifest/test_manifest.cpp
git commit -m "sp6a: parse states (string/array) with truncation warning"
```

---

### Task 5: Unknown states + full bufo fixture smoke

**Files:**
- Modify: `test/test_manifest/test_manifest.cpp`

(Implementation already handles unknown states. This task just proves it via tests and adds the full-bufo smoke.)

- [ ] **Step 1: Add tests**

```cpp
static const char* kBufoReal = R"({
  "name": "bufo",
  "colors": { "body":"#6B8E23","bg":"#000000","text":"#FFFFFF",
              "textDim":"#808080","ink":"#000000" },
  "states": {
    "sleep": "sleep.gif",
    "idle":  ["idle_0.gif","idle_1.gif","idle_2.gif","idle_3.gif",
              "idle_4.gif","idle_5.gif","idle_6.gif","idle_7.gif","idle_8.gif"],
    "busy":"busy.gif","attention":"attention.gif",
    "celebrate":"celebrate.gif","dizzy":"dizzy.gif","heart":"heart.gif"
  }
})";

void test_parse_unknown_state_ignored() {
  const char* j = R"({"name":"x","colors":{"body":"#000000","bg":"#000000",
    "text":"#000000","textDim":"#000000","ink":"#000000"},
    "states":{"dancing":"d.gif","idle":"i.gif"}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_TRUE(err.empty());  // unknown is not an error
}

void test_parse_real_bufo_manifest() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoReal, std::strlen(kBufoReal), m, err));
  TEST_ASSERT_EQUAL_STRING("bufo", m.name);
  TEST_ASSERT_EQUAL_UINT8(9, m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_EQUAL_STRING("idle_8.gif", m.states[MANIFEST_STATE_IDLE][8]);
  TEST_ASSERT_EQUAL_UINT8(0, m.stateVariantCount[MANIFEST_STATE_NAP]);
}
```

Register:

```cpp
RUN_TEST(test_parse_unknown_state_ignored);
RUN_TEST(test_parse_real_bufo_manifest);
```

- [ ] **Step 2: Run tests, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: 15 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add test/test_manifest/test_manifest.cpp
git commit -m "sp6a: cover unknown-state + full bufo manifest"
```

---

### Task 6: Active-cache API (`manifestSetActive` / `manifestActive`)

This task covers the pure-C++ side of the cache (from a JSON string).
`manifestSetActive` reading from SFUD is ARDUINO-only and ships in Task 8.

**Files:**
- Modify: `src/manifest.cpp`
- Modify: `test/test_manifest/test_manifest.cpp`

- [ ] **Step 1: Add failing tests**

```cpp
void test_active_set_and_get_roundtrip() {
  _manifestResetForTest();
  TEST_ASSERT_NULL(manifestActive());
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(kBufoReal,
                                              std::strlen(kBufoReal)));
  const CharManifest* m = manifestActive();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("bufo", m->name);
}

void test_active_failed_parse_leaves_prior_intact() {
  _manifestResetForTest();
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(kBufoReal,
                                              std::strlen(kBufoReal)));
  const char* bad = "{not json";
  TEST_ASSERT_FALSE(_manifestSetActiveFromJson(bad, std::strlen(bad)));
  const CharManifest* m = manifestActive();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("bufo", m->name);  // prior stayed
}
```

Register both.

- [ ] **Step 2: Run tests, verify second test fails**

```bash
pio test -e native -f test_manifest
```

Expected: `test_active_failed_parse_leaves_prior_intact` FAILS (current `_manifestSetActiveFromJson` stub overwrites `active` before checking parse result).

- [ ] **Step 3: Fix `_manifestSetActiveFromJson` to parse into a staging buffer**

In `src/manifest.cpp`, replace the `#ifndef ARDUINO` test helper:

```cpp
#ifndef ARDUINO
void _manifestResetForTest() {
  hasActive = false;
  std::memset(&active, 0, sizeof(active));
}
bool _manifestSetActiveFromJson(const char* json, size_t len) {
  CharManifest staging;
  std::string err;
  if (!manifestParseJson(json, len, staging, err)) return false;
  active = staging;
  hasActive = true;
  return true;
}
#endif
```

- [ ] **Step 4: Run tests, verify pass**

```bash
pio test -e native -f test_manifest
```

Expected: 17 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/manifest.cpp test/test_manifest/test_manifest.cpp
git commit -m "sp6a: active-manifest cache with atomic replace"
```

---

### Task 7: Persist `activeCharName` field + version bump

**Files:**
- Modify: `src/persist.h`
- Modify: `src/persist.cpp`
- Modify: `test/test_persist/test_persist.cpp`

- [ ] **Step 1: Add failing tests**

In `test/test_persist/test_persist.cpp`, append:

```cpp
void test_active_char_roundtrip() {
  _persistResetFakeFile();
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());

  persistSetActiveChar("bufo");
  persistTick(0);   // force flush of forceFlushNextTick
  persistTick(PERSIST_DEBOUNCE_MS + 1);

  // Re-init simulates a reboot.
  persistInit();
  TEST_ASSERT_EQUAL_STRING("bufo", persistGetActiveChar());
}

void test_active_char_default_empty_on_fresh_init() {
  _persistResetFakeFile();
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());
}

void test_v1_sized_blob_falls_to_defaults() {
  // Simulate a stored blob from before activeCharName was added.
  // The size-mismatch guard in readStore must reject it so setDefaults
  // runs and we don't read garbage out of the new field.
  _persistResetFakeFile();
  uint8_t fake[32] = {0};   // small, clearly not sizeof(PersistData)
  _persistSeedFakeFile(fake, sizeof(fake));
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, persistGet().magic);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, persistGet().version);
}
```

Register all three in `main`.

- [ ] **Step 2: Run tests, verify compile failure**

```bash
pio test -e native -f test_persist
```

Expected: undefined `persistSetActiveChar` / `persistGetActiveChar`.

- [ ] **Step 3: Modify `src/persist.h`**

Find the `PersistData` struct (the one with `appr`, `deny`, etc.) and add a trailing field:

```cpp
struct PersistData {
  uint32_t magic;
  uint32_t version;
  // ...existing fields unchanged...
  char     activeCharName[33];
};
```

Add declarations below the existing setters:

```cpp
void persistSetActiveChar(const char* name);
const char* persistGetActiveChar();

#ifndef ARDUINO
// Test-only: overwrite the native fake flash with arbitrary bytes so
// tests can simulate pre-migration blobs.
void _persistSeedFakeFile(const uint8_t* bytes, size_t n);
#endif
```

- [ ] **Step 4: Modify `src/config.h`**

Bump version:

```cpp
static constexpr uint32_t PERSIST_VERSION = 2;
```

- [ ] **Step 5: Modify `src/persist.cpp`**

Add at the bottom of the file:

```cpp
void persistSetActiveChar(const char* name) {
  std::strncpy(data.activeCharName, name ? name : "",
               sizeof(data.activeCharName) - 1);
  data.activeCharName[sizeof(data.activeCharName) - 1] = '\0';
  persistCommit(true);
}

const char* persistGetActiveChar() {
  return data.activeCharName;
}

#ifndef ARDUINO
void _persistSeedFakeFile(const uint8_t* bytes, size_t n) {
  fakeFile.assign(bytes, bytes + n);
}
#endif
```

No other changes needed — `setDefaults()` already zeroes the struct, which leaves `activeCharName` as an empty C-string. The size-mismatch guard in `readStore` plus the version check handles v1 blobs correctly (both trigger `setDefaults()`).

- [ ] **Step 6: Run `test_persist`, verify pass**

```bash
pio test -e native -f test_persist
```

Expected: previous tests + 2 new tests all PASS.

- [ ] **Step 7: Commit**

```bash
git add src/persist.h src/persist.cpp src/config.h test/test_persist/test_persist.cpp
git commit -m "sp6a: persist activeCharName + bump PERSIST_VERSION to 2"
```

---

### Task 8: Wire `xferEndChar` + `main::setup` (ARDUINO-only)

Native `test_xfer` is NOT touched — `xferEndChar` under native still returns true without touching SFUD or manifest, which keeps all existing tests green. The ARDUINO path adds the integration.

**Files:**
- Modify: `src/manifest.cpp`
- Modify: `src/xfer.cpp::xferEndChar`
- Modify: `src/main.cpp::setup`

- [ ] **Step 1: Implement `manifestSetActive` (ARDUINO-only) + `manifestParseFile`**

In `src/manifest.cpp`, add at the top:

```cpp
#ifdef ARDUINO
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#endif
```

Below the existing API, add:

```cpp
#ifdef ARDUINO
bool manifestParseFile(const char* path, CharManifest& out, std::string& err) {
  File f = SFUD.open(path, FILE_READ);
  if (!f) { err = "open failed"; return false; }
  size_t size = f.size();
  if (size == 0 || size > 8192) {
    f.close(); err = "size out of range"; return false;
  }
  // Read into a heap buffer; stack is tight next to rpcBLE.
  std::string buf;
  buf.resize(size);
  size_t n = f.read(reinterpret_cast<uint8_t*>(&buf[0]), size);
  f.close();
  if (n != size) { err = "short read"; return false; }
  return manifestParseJson(buf.data(), buf.size(), out, err);
}

bool manifestSetActive(const char* charName) {
  if (!charName || !charName[0]) return false;
  char path[96];
  std::snprintf(path, sizeof(path), "/chars/%s/manifest.json", charName);
  CharManifest staging;
  std::string err;
  if (!manifestParseFile(path, staging, err)) return false;
  active = staging;
  hasActive = true;
  return true;
}
#else
bool manifestSetActive(const char*) { return false; }
#endif
```

Also add to `src/manifest.h` (above the `#ifndef ARDUINO` block):

```cpp
#ifdef ARDUINO
bool manifestParseFile(const char* path, CharManifest& out, std::string& err);
#endif
```

- [ ] **Step 2: Wire `xferEndChar` to parse + persist**

In `src/xfer.cpp`, add at the top with the other includes:

```cpp
#include "manifest.h"
#ifdef ARDUINO
#include "persist.h"
#endif
```

Replace the current `xferEndChar` body:

```cpp
bool xferEndChar() {
  if (state != State::CharOpen) return false;
  state = State::Idle;
#ifdef ARDUINO
  // charName is still latched; treat a bad/missing manifest as an upload
  // failure so the host can retry. Prior active char is preserved.
  if (!manifestSetActive(charName)) return false;
  persistSetActiveChar(charName);
#endif
  return true;
}
```

- [ ] **Step 3: Wire `main::setup` to rehydrate**

In `src/main.cpp::setup`, after the block that calls `persistInit();`, add:

```cpp
  if (persistGetActiveChar()[0] != '\0') {
    manifestSetActive(persistGetActiveChar());  // best-effort
  }
```

Also add to main.cpp's includes (near the other local headers):

```cpp
#include "manifest.h"
```

- [ ] **Step 4: Run all native tests, verify green**

```bash
pio test -e native
```

Expected: all pre-existing tests + new manifest/persist tests PASS. The xfer native tests must still pass unchanged because the manifest code is ARDUINO-only.

- [ ] **Step 5: Build firmware, verify no link errors**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. RAM usage bumps by ~4.3 KB bss; note the new percentage.

- [ ] **Step 6: Commit**

```bash
git add src/manifest.h src/manifest.cpp src/xfer.cpp src/main.cpp
git commit -m "sp6a: wire xferEndChar + boot rehydrate to manifest cache"
```

---

### Task 9: Device smoke + merge

**Files:** none (verification only).

- [ ] **Step 1: Flash new firmware**

```bash
pio run -e seeed_wio_terminal -t upload
```

Expected: SUCCESS, device reboots into Advertising.

- [ ] **Step 2: Upload `characters/bufo` via Hardware Buddy (= spec's xfer-integration check)**

Native `test_xfer` is intentionally NOT extended — the manifest glue is
ARDUINO-only, so integration is verified on-device instead.

- Connect via the desktop app (`Claude-<suffix>`).
- Drop the `characters/bufo` folder into the app.
- Click "Send to Device".
- **Expected:**
  - Upload completes with no `file ack timeout`.
  - Final `char_end` ack is `ok:true` (= manifest parse succeeded on device).
  - The on-screen ASCII pet does NOT change (by design — rendering is SP6b).
- **Negative case** (optional, if you have a junk folder handy):
  - Upload a folder containing a `manifest.json` with no `"colors"` key.
  - Expect: `char_end` ack is `ok:false`, prior active char remains.

- [ ] **Step 3: Verify active char persists across reboot**

- Physically reset the Wio Terminal.
- Via the serial monitor, look for `[persist]` output (no errors).
- Reconnect with Hardware Buddy, issue a `status` command.
- **Expected:** device is in Advertising → Connected; no regressions.
- (Since there's no exposed "current active char" status field yet in SP1's status schema, this step is mostly visual: no crash, no `file ack timeout`, ASCII pet rendering continues.)

- [ ] **Step 4: Merge to `main`**

```bash
git checkout main
git merge --no-ff feature/sp6a-manifest-active-char \
  -m "Merge feature/sp6a-manifest-active-char: manifest parse + active-char persist"
```

- [ ] **Step 5: (Optional) Delete local branch**

```bash
git branch -d feature/sp6a-manifest-active-char
```

---

## Verification checklist

After the full plan executes, confirm:

- [ ] `pio test -e native` passes 100% (prior 102 + new manifest + new persist = ~119).
- [ ] `pio run -e seeed_wio_terminal` builds clean. Flash size unchanged ±1%; RAM bumps ≤5 KB.
- [ ] Uploading `characters/bufo` returns success ack (no `file ack timeout` or similar).
- [ ] Reboot preserves active char (persist write went through).
- [ ] ASCII pet rendering unchanged (SP6b has not landed).
