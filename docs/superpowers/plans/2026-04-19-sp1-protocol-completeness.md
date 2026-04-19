# SP1 Protocol Completeness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make firmware recognize and respond to every message type in the reference BLE protocol (`status` / `name` / `unpair` / `owner` cmds, `turn` event, extended heartbeat fields) without touching UI, persistence, or security. Ship a firmware that populates Claude Desktop's Hardware Buddy stats panel and doesn't regress MVP A.

**Architecture:** Extend existing `protocol.cpp` / `state.cpp` with new `MessageKind` variants and a generic `formatAck` helper. Add a new `status.cpp` / `status.h` pair that isolates platform-dependent data capture (`millis()`, free heap) from native-testable JSON serialization (`formatStatusAck`). Wire new dispatches in `main.cpp::onLine`.

**Tech Stack:** Arduino framework, PlatformIO, Seeed_Arduino_rpcBLE, ArduinoJson v6, Unity test framework on `[env:native]`.

**Spec:** `docs/superpowers/specs/2026-04-19-sp1-protocol-completeness-design.md`

---

## Prerequisites

- MVP A merged to `main` (commit `c646878`) — done
- spec committed (commit `7fa4d66`) — done
- SAMD51 toolchain via PlatformIO working — verify with `pio run -e seeed_wio_terminal` before starting
- Wio Terminal at hand with RTL8720DN firmware updated (required for BLE)

---

## Task 0: Branch setup

**Files:**
- No code changes

- [ ] **Step 1: Create feature branch**

Run:
```bash
git checkout -b feature/sp1-protocol-completeness
git status
```
Expected: `On branch feature/sp1-protocol-completeness`, clean tree.

- [ ] **Step 2: Verify baseline builds and tests pass**

Run:
```bash
pio run -e seeed_wio_terminal
pio test -e native
```
Expected: Both succeed. If `pio run` fails, stop and resolve before continuing.

---

## Task 1: Spike — SAMD51 free heap helper

**Risk mitigated:** R2 (no FreeRTOS on main MCU, so `xPortGetFreeHeapSize()` is unavailable).

**Files:**
- Create: `src/mem.h`
- Create: `src/mem.cpp`

- [ ] **Step 1: Create `src/mem.h`**

```cpp
#pragma once

#include <cstdint>

// Free heap bytes on the SAMD51 main MCU (not FreeRTOS).
// Uses linker symbols + current stack pointer.
uint32_t freeHeapBytes();
```

- [ ] **Step 2: Create `src/mem.cpp`**

```cpp
#include "mem.h"

extern "C" char* sbrk(int incr);
extern char _end;

uint32_t freeHeapBytes() {
  char stack_var;
  char* heap_end = sbrk(0);
  // Distance from current top-of-heap to current stack pointer
  // approximates the free RAM between them.
  return static_cast<uint32_t>(&stack_var - heap_end);
}
```

- [ ] **Step 3: Temporarily call from `setup()` and print to serial**

Modify `src/main.cpp` — add `#include "mem.h"` and, inside `setup()` just before `initBle(...)`:

```cpp
  Serial.print("[mem] free heap at setup: ");
  Serial.println(freeHeapBytes());
```

- [ ] **Step 4: Upload and verify output**

Run:
```bash
pio run -e seeed_wio_terminal -t upload
pio device monitor -e seeed_wio_terminal --baud 115200
```
Expected: Serial prints a plausible number (tens of thousands of bytes, e.g. 80000+). If number is 0, negative, or absurdly large, sbrk/linker-symbol approach needs revision — investigate Seeed BSP for a maintained `freeMemory()` and use that instead.

- [ ] **Step 5: Remove the debug print from setup**

Edit `src/main.cpp` to remove the two debug lines added in Step 3. Leave the `#include "mem.h"` — we'll use it later.

- [ ] **Step 6: Commit**

```bash
git add src/mem.h src/mem.cpp src/main.cpp
git commit -m "feat: add freeHeapBytes() helper for SAMD51 main MCU

Uses sbrk(0) and current stack pointer to estimate free RAM.
Verified on-device output is in plausible range."
```

---

## Task 2: Spike — verify sendLine handles status-ack-sized payload

**Risk mitigated:** Q-OPEN-2 (status ack ~200 bytes, MTU default ~20 bytes; need to confirm rpcBLE auto-fragments notifications).

**Files:**
- Modify: `src/main.cpp` (temporary test code, reverted in step 4)

- [ ] **Step 1: Add temporary send test in `setup()`**

In `src/main.cpp`, just after `appState.mode = Mode::Advertising;` and before `render(true);`, temporarily add:

```cpp
  // TEMPORARY — remove after spike
  while (!isBleConnected()) { pollBle(); delay(100); }
  std::string fakeAck =
      R"({"ack":"status","ok":true,"data":{"name":"Claude-XXXX","sec":false,"bat":{"pct":100,"mV":5000,"mA":0,"usb":true},"sys":{"up":12,"heap":80000},"stats":{"appr":0,"deny":0,"vel":0,"nap":0,"lvl":0}}})"
      "\n";
  Serial.print("[spike] sending ack bytes=");
  Serial.println(fakeAck.size());
  bool ok = sendLine(fakeAck);
  Serial.print("[spike] sendLine returned: ");
  Serial.println(ok ? "true" : "false");
```

- [ ] **Step 2: Upload, connect from Mac (Bluetility or nRF Connect), subscribe to TX notify**

Run:
```bash
pio run -e seeed_wio_terminal -t upload
pio device monitor -e seeed_wio_terminal --baud 115200
```
Then in Bluetility: connect to `Claude-XXXX`, enable notifications on NUS TX characteristic.

Expected:
- Serial shows `[spike] sendLine returned: true`
- Bluetility's TX characteristic receives the full JSON string reassembled (multiple notification packets concatenated by the central)
- JSON ends with `\n`

- [ ] **Step 3: Record result**

If reassembly works on central side → proceed, no code change needed. If Bluetility receives only first 20 bytes → need to add chunking in `sendLine`; open follow-up issue and stop here to reassess.

- [ ] **Step 4: Revert the temporary code**

Remove the `while (!isBleConnected())...` block added in Step 1.

- [ ] **Step 5: Commit (no code change from baseline, so no commit — just leave branch clean)**

Run:
```bash
git status
```
Expected: clean (the revert returned to pre-spike state). If not clean, verify diff is only the spike block and run `git checkout -- src/main.cpp`.

---

## Task 3: Config constants and platformio.ini native build filter

**Files:**
- Modify: `src/config.h`
- Modify: `platformio.ini`

- [ ] **Step 1: Add constants to `src/config.h`**

Append to the end of `src/config.h` (before last `#endif` if present, otherwise just append):

```cpp
// --- SP1 protocol completeness ---
static constexpr size_t ENTRIES_MAX      = 5;
static constexpr size_t ENTRY_CHARS_MAX  = 128;
static constexpr size_t NAME_CHARS_MAX   = 32;
static constexpr size_t STATUS_ACK_BUF   = 512;
```

- [ ] **Step 2: Extend native build filter in `platformio.ini`**

Change `[env:native]` `build_src_filter` line from:
```ini
build_src_filter = +<state.cpp> +<protocol.cpp>
```
to:
```ini
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp>
```

Note: `status.cpp` doesn't exist yet — we'll create it in Task 10. `pio test -e native` will fail until then, so we keep Task 3 ↔ Task 10 ordering in mind.

- [ ] **Step 3: Verify device build still passes**

Run:
```bash
pio run -e seeed_wio_terminal
```
Expected: success (native env not built yet).

- [ ] **Step 4: Commit**

```bash
git add src/config.h platformio.ini
git commit -m "feat(sp1): add protocol config constants and native build filter

ENTRIES_MAX=5, ENTRY_CHARS_MAX=128, NAME_CHARS_MAX=32, STATUS_ACK_BUF=512.
Native build filter includes status.cpp (added in later task)."
```

---

## Task 4: Split TurnEvent from Unknown in parser

**Files:**
- Modify: `src/protocol.h`
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Update the existing turn-event test to expect `TurnEvent`**

In `test/test_protocol/test_protocol.cpp`, replace the existing test `test_parse_turn_event_is_unknown`:

```cpp
void test_parse_turn_event() {
  std::string line = R"({"evt":"turn","role":"assistant","content":[]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::TurnEvent),
                    static_cast<int>(m.kind));
}
```

And in `main(int, char**)` change the corresponding `RUN_TEST` line to:

```cpp
  RUN_TEST(test_parse_turn_event);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
pio test -e native -f test_protocol
```
Expected: FAIL — `MessageKind::TurnEvent` does not exist.

- [ ] **Step 3: Add `TurnEvent` to the enum in `src/protocol.h`**

Replace the `MessageKind` enum (keep existing members, add one):

```cpp
enum class MessageKind {
  Heartbeat,
  Owner,
  Time,
  TurnEvent,
  Unknown,
  ParseError,
};
```

Also remove the trailing comment `// includes "evt":"turn" and anything else we ignore` since it's no longer accurate.

- [ ] **Step 4: Recognize `evt:"turn"` in `parseLine`**

In `src/protocol.cpp`, inside `parseLine`, after the existing `if (doc["cmd"] == "owner")` block but before the `if (doc.containsKey("time")...)` block, add:

```cpp
  if (doc["evt"] == "turn") {
    m.kind = MessageKind::TurnEvent;
    return m;
  }
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
pio test -e native -f test_protocol
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/protocol.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): parser recognizes turn events separately from unknown"
```

---

## Task 5: Parser recognizes StatusCmd and UnpairCmd

**Files:**
- Modify: `src/protocol.h`
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Write failing tests**

Append to `test/test_protocol/test_protocol.cpp` (before `int main`):

```cpp
void test_parse_status_cmd() {
  std::string line = R"({"cmd":"status"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::StatusCmd),
                    static_cast<int>(m.kind));
}

void test_parse_unpair_cmd() {
  std::string line = R"({"cmd":"unpair"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::UnpairCmd),
                    static_cast<int>(m.kind));
}
```

And add `RUN_TEST(test_parse_status_cmd);` and `RUN_TEST(test_parse_unpair_cmd);` to `main()`.

- [ ] **Step 2: Run to verify fails**

Run: `pio test -e native -f test_protocol`
Expected: FAIL — `StatusCmd` / `UnpairCmd` not in enum.

- [ ] **Step 3: Extend `MessageKind` enum**

In `src/protocol.h` enum:

```cpp
enum class MessageKind {
  Heartbeat,
  Owner,
  Time,
  TurnEvent,
  StatusCmd,
  NameCmd,
  UnpairCmd,
  Unknown,
  ParseError,
};
```

(`NameCmd` added here too; we'll use it in Task 6.)

- [ ] **Step 4: Recognize cmds in `parseLine`**

In `src/protocol.cpp`, add after the existing `owner` block:

```cpp
  if (doc["cmd"] == "status") {
    m.kind = MessageKind::StatusCmd;
    return m;
  }
  if (doc["cmd"] == "unpair") {
    m.kind = MessageKind::UnpairCmd;
    return m;
  }
```

- [ ] **Step 5: Run to verify passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS (StatusCmd / UnpairCmd tests green; NameCmd test doesn't exist yet, so no failure).

- [ ] **Step 6: Commit**

```bash
git add src/protocol.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): parser recognizes status and unpair commands"
```

---

## Task 6: Parser recognizes NameCmd and captures name value

**Files:**
- Modify: `src/protocol.h`
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Add `nameValue` field to `ParsedMessage`**

In `src/protocol.h`, change `ParsedMessage` struct to:

```cpp
struct ParsedMessage {
  MessageKind kind = MessageKind::Unknown;
  HeartbeatData heartbeat;
  std::string ownerName;
  std::string nameValue;        // for NameCmd
  int32_t timeEpoch = 0;
  int32_t timeOffsetSec = 0;
};
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
void test_parse_name_cmd_normal() {
  std::string line = R"({"cmd":"name","name":"Clawd"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::NameCmd),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("Clawd", m.nameValue.c_str());
}

void test_parse_name_cmd_empty() {
  std::string line = R"({"cmd":"name","name":""})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::NameCmd),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("", m.nameValue.c_str());
}
```

Add `RUN_TEST(test_parse_name_cmd_normal);` and `RUN_TEST(test_parse_name_cmd_empty);` to `main()`.

- [ ] **Step 3: Run to verify fails**

Run: `pio test -e native -f test_protocol`
Expected: FAIL — `NameCmd` not handled in `parseLine`.

- [ ] **Step 4: Recognize `name` cmd in `parseLine`**

In `src/protocol.cpp`, add after the `unpair` block:

```cpp
  if (doc["cmd"] == "name") {
    m.kind = MessageKind::NameCmd;
    m.nameValue = doc["name"] | "";
    return m;
  }
```

- [ ] **Step 5: Run to verify passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/protocol.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): parser recognizes name command and captures value"
```

---

## Task 7: Heartbeat parser captures entries with truncation

**Files:**
- Modify: `src/state.h` (extend `HeartbeatData`)
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Extend `HeartbeatData` in `src/state.h`**

Add `#include <vector>` near the top (after `#include <string>`). Change `HeartbeatData`:

```cpp
struct HeartbeatData {
  int total = 0;
  int running = 0;
  int waiting = 0;
  std::string msg;
  std::vector<std::string> entries;
  bool hasPrompt = false;
  PromptData prompt;
};
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
void test_parse_heartbeat_entries() {
  std::string line =
      R"({"total":1,"running":1,"waiting":0,"msg":"busy",)"
      R"("entries":["10:42 a","10:41 b","10:40 c"]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(3u, m.heartbeat.entries.size());
  TEST_ASSERT_EQUAL_STRING("10:42 a", m.heartbeat.entries[0].c_str());
  TEST_ASSERT_EQUAL_STRING("10:40 c", m.heartbeat.entries[2].c_str());
}

void test_parse_heartbeat_entries_truncate_count() {
  // 7 entries, ENTRIES_MAX=5 should keep first 5.
  std::string line =
      R"({"total":1,"running":0,"waiting":0,)"
      R"("entries":["a","b","c","d","e","f","g"]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(5u, m.heartbeat.entries.size());
  TEST_ASSERT_EQUAL_STRING("e", m.heartbeat.entries[4].c_str());
}

void test_parse_heartbeat_entries_truncate_chars() {
  std::string big(200, 'x');  // 200 chars
  std::string line =
      R"({"total":1,"running":0,"waiting":0,"entries":[")" + big + R"("]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(1u, m.heartbeat.entries.size());
  TEST_ASSERT_EQUAL(128u, m.heartbeat.entries[0].size());
}

void test_parse_heartbeat_no_entries_key() {
  std::string line = R"({"total":1,"running":0,"waiting":0})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_TRUE(m.heartbeat.entries.empty());
}
```

Add four corresponding `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 3: Run to verify fails**

Run: `pio test -e native -f test_protocol`
Expected: FAIL — `entries` not parsed.

- [ ] **Step 4: Parse entries in `parseLine`**

In `src/protocol.cpp`, add `#include "config.h"` near the top (after existing includes). Inside the heartbeat branch, after setting `msg`, before `if (doc.containsKey("prompt")...)`, add:

```cpp
    if (doc.containsKey("entries") && doc["entries"].is<JsonArray>()) {
      JsonArray arr = doc["entries"].as<JsonArray>();
      size_t n = 0;
      for (JsonVariant v : arr) {
        if (n >= ENTRIES_MAX) break;
        const char* s = v | "";
        std::string entry(s);
        if (entry.size() > ENTRY_CHARS_MAX) entry.resize(ENTRY_CHARS_MAX);
        m.heartbeat.entries.push_back(std::move(entry));
        ++n;
      }
    }
```

- [ ] **Step 5: Run to verify passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/state.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): heartbeat parser captures entries with size truncation

Caps at ENTRIES_MAX=5 items, each truncated to ENTRY_CHARS_MAX=128."
```

---

## Task 8: Heartbeat parser captures tokens and tokens_today

**Files:**
- Modify: `src/state.h`
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Extend `HeartbeatData` with token fields**

In `src/state.h`, update `HeartbeatData`:

```cpp
struct HeartbeatData {
  int total = 0;
  int running = 0;
  int waiting = 0;
  std::string msg;
  std::vector<std::string> entries;
  int64_t tokens = 0;
  int64_t tokens_today = 0;
  bool hasPrompt = false;
  PromptData prompt;
};
```

- [ ] **Step 2: Write failing test**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
void test_parse_heartbeat_tokens() {
  std::string line =
      R"({"total":1,"running":0,"waiting":0,"tokens":184502,"tokens_today":31200})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL_INT64(184502, m.heartbeat.tokens);
  TEST_ASSERT_EQUAL_INT64(31200, m.heartbeat.tokens_today);
}

void test_parse_heartbeat_tokens_missing_defaults_zero() {
  std::string line = R"({"total":1,"running":0,"waiting":0})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL_INT64(0, m.heartbeat.tokens);
  TEST_ASSERT_EQUAL_INT64(0, m.heartbeat.tokens_today);
}
```

Add the two `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 3: Run to verify fails**

Run: `pio test -e native -f test_protocol`
Expected: FAIL — token fields not read.

- [ ] **Step 4: Parse tokens in `parseLine`**

In `src/protocol.cpp`, inside heartbeat branch, after the entries block, before `prompt` block, add:

```cpp
    m.heartbeat.tokens       = doc["tokens"]       | (int64_t)0;
    m.heartbeat.tokens_today = doc["tokens_today"] | (int64_t)0;
```

- [ ] **Step 5: Run to verify passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/state.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): heartbeat parser captures tokens and tokens_today"
```

---

## Task 9: formatAck helper

**Files:**
- Modify: `src/protocol.h`
- Modify: `src/protocol.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Declare `formatAck` in `src/protocol.h`**

Append at the bottom of the file (before any closing `#endif`):

```cpp
// Build a generic ack JSON line terminated with '\n'.
// If `error` is non-empty, it's included as `"error": "..."`.
std::string formatAck(const std::string& cmd, bool ok,
                      const std::string& error = "");
```

- [ ] **Step 2: Write failing tests**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
void test_format_ack_ok() {
  std::string out = formatAck("owner", true);
  TEST_ASSERT_EQUAL_STRING(R"({"ack":"owner","ok":true})" "\n", out.c_str());
}

void test_format_ack_err() {
  std::string out = formatAck("name", false, "empty name");
  TEST_ASSERT_EQUAL_STRING(
      R"({"ack":"name","ok":false,"error":"empty name"})" "\n",
      out.c_str());
}
```

Add the two `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 3: Run to verify fails**

Run: `pio test -e native -f test_protocol`
Expected: FAIL — `formatAck` unresolved.

- [ ] **Step 4: Implement `formatAck` in `src/protocol.cpp`**

Append to the bottom of the file:

```cpp
std::string formatAck(const std::string& cmd, bool ok,
                      const std::string& error) {
  StaticJsonDocument<256> doc;
  doc["ack"] = cmd;
  doc["ok"] = ok;
  if (!error.empty()) doc["error"] = error;
  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}
```

- [ ] **Step 5: Run to verify passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/protocol.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): add formatAck helper for generic cmd acks"
```

---

## Task 10: StatusSnapshot struct and formatStatusAck

**Files:**
- Create: `src/status.h`
- Create: `src/status.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Create `src/status.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

struct AppState;

struct StatusSnapshot {
  std::string name;
  bool        sec = false;
  uint32_t    upSec = 0;
  uint32_t    heapFree = 0;
};

// Platform-specific: reads millis() and free heap. Only compiled on device.
StatusSnapshot captureStatus(const AppState& s, uint32_t nowMs);

// Pure logic: serializes snapshot to JSON line. Native-testable.
std::string formatStatusAck(const StatusSnapshot& snap);
```

- [ ] **Step 2: Create `src/status.cpp` with the native-testable `formatStatusAck` only**

```cpp
#include "status.h"
#include <ArduinoJson.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "mem.h"
#include "state.h"
#endif

std::string formatStatusAck(const StatusSnapshot& snap) {
  StaticJsonDocument<512> doc;
  doc["ack"] = "status";
  doc["ok"] = true;
  JsonObject data = doc.createNestedObject("data");
  data["name"] = snap.name;
  data["sec"] = snap.sec;

  JsonObject bat = data.createNestedObject("bat");
  bat["pct"] = 100;
  bat["mV"]  = 5000;
  bat["mA"]  = 0;
  bat["usb"] = true;

  JsonObject sys = data.createNestedObject("sys");
  sys["up"]   = snap.upSec;
  sys["heap"] = snap.heapFree;

  JsonObject stats = data.createNestedObject("stats");
  stats["appr"] = 0;
  stats["deny"] = 0;
  stats["vel"]  = 0;
  stats["nap"]  = 0;
  stats["lvl"]  = 0;

  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}

#ifdef ARDUINO
StatusSnapshot captureStatus(const AppState& s, uint32_t nowMs) {
  StatusSnapshot snap;
  snap.name = s.deviceName;
  snap.sec = false;
  snap.upSec = nowMs / 1000;
  snap.heapFree = freeHeapBytes();
  return snap;
}
#endif
```

Note: `captureStatus` references `AppState::deviceName` which we add in Task 11. For now the `#ifdef ARDUINO` guard means native builds skip it — device builds will fail until Task 11 lands, so keep Task 10 → Task 11 ordering.

- [ ] **Step 3: Write failing test for `formatStatusAck`**

Append to `test/test_protocol/test_protocol.cpp`:

```cpp
#include "status.h"

void test_format_status_ack_shape() {
  StatusSnapshot snap;
  snap.name = "Claude-52DA";
  snap.sec = false;
  snap.upSec = 12;
  snap.heapFree = 80000;
  std::string out = formatStatusAck(snap);
  // Spot-check key fragments; order is deterministic with ArduinoJson.
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("ack":"status")"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("ok":true)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("name":"Claude-52DA")"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("sec":false)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("usb":true)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("up":12)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("heap":80000)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("lvl":0)"));
  TEST_ASSERT_EQUAL('\n', out.back());
}
```

Add `RUN_TEST(test_format_status_ack_shape);` to `main()`. Also add `#include <cstring>` near the top of the test file if not already present.

- [ ] **Step 4: Run to verify fails → passes**

Run: `pio test -e native -f test_protocol`
Expected: PASS (status.cpp is in native build filter from Task 3).

- [ ] **Step 5: Commit**

```bash
git add src/status.h src/status.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(sp1): add StatusSnapshot and formatStatusAck

formatStatusAck is native-testable pure logic. captureStatus
is device-only (guarded by ARDUINO) and will be wired in next task."
```

---

## Task 11: AppState extensions + applyNameCmd + applyTime

**Files:**
- Modify: `src/state.h`
- Modify: `src/state.cpp`
- Create: `test/test_state_sp1/test_state_sp1.cpp`

- [ ] **Step 1: Extend `AppState` in `src/state.h`**

Update the struct:

```cpp
struct AppState {
  Mode mode = Mode::BleInit;
  HeartbeatData hb;
  std::string ownerName;
  std::string deviceName;       // initial "Claude-XXXX"; name cmd updates in-memory
  int64_t     timeEpoch = 0;
  int32_t     timeOffsetSec = 0;
  uint32_t    timeSetAtMs = 0;
  uint32_t lastHeartbeatMs = 0;
  bool ackApproved = false;
  uint32_t ackUntilMs = 0;
};
```

- [ ] **Step 2: Declare `applyNameCmd` and `applyTime` in `src/state.h`**

Append to the bottom (before closing `#pragma once` scope — just add at end of file):

```cpp
// Update deviceName in-memory. Rejects empty; truncates to NAME_CHARS_MAX.
// Returns ok: true if accepted, false if rejected. `err` is set on rejection.
bool applyNameCmd(AppState& s, const std::string& name, std::string& err);

// Store time sync (epoch + tz offset + local millis stamp). Returns true.
bool applyTime(AppState& s, int64_t epoch, int32_t offsetSec, uint32_t nowMs);
```

- [ ] **Step 3: Create `test/test_state_sp1/test_state_sp1.cpp` with failing tests**

```cpp
#include <unity.h>
#include "state.h"
#include "config.h"

void test_apply_name_normal() {
  AppState s;
  std::string err;
  bool ok = applyNameCmd(s, "Clawd", err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Clawd", s.deviceName.c_str());
  TEST_ASSERT_TRUE(err.empty());
}

void test_apply_name_empty_rejected() {
  AppState s;
  s.deviceName = "OldName";
  std::string err;
  bool ok = applyNameCmd(s, "", err);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("OldName", s.deviceName.c_str());
  TEST_ASSERT_EQUAL_STRING("empty name", err.c_str());
}

void test_apply_name_truncates() {
  AppState s;
  std::string err;
  std::string longName(50, 'x');  // 50 chars, NAME_CHARS_MAX=32
  bool ok = applyNameCmd(s, longName, err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(NAME_CHARS_MAX, s.deviceName.size());
}

void test_apply_time_stores_values() {
  AppState s;
  bool changed = applyTime(s, 1775731234, -25200, 9876);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL_INT64(1775731234, s.timeEpoch);
  TEST_ASSERT_EQUAL_INT32(-25200, s.timeOffsetSec);
  TEST_ASSERT_EQUAL_UINT32(9876, s.timeSetAtMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_name_normal);
  RUN_TEST(test_apply_name_empty_rejected);
  RUN_TEST(test_apply_name_truncates);
  RUN_TEST(test_apply_time_stores_values);
  return UNITY_END();
}
```

- [ ] **Step 4: Run to verify fails**

Run: `pio test -e native -f test_state_sp1`
Expected: FAIL — functions unresolved.

- [ ] **Step 5: Implement in `src/state.cpp`**

Append to the file:

```cpp
bool applyNameCmd(AppState& s, const std::string& name, std::string& err) {
  if (name.empty()) {
    err = "empty name";
    return false;
  }
  std::string n = name;
  if (n.size() > NAME_CHARS_MAX) n.resize(NAME_CHARS_MAX);
  s.deviceName = std::move(n);
  return true;
}

bool applyTime(AppState& s, int64_t epoch, int32_t offsetSec, uint32_t nowMs) {
  s.timeEpoch = epoch;
  s.timeOffsetSec = offsetSec;
  s.timeSetAtMs = nowMs;
  return true;
}
```

- [ ] **Step 6: Run to verify passes**

Run:
```bash
pio test -e native
```
Expected: all test suites PASS (protocol + state + state_sp1).

- [ ] **Step 7: Commit**

```bash
git add src/state.h src/state.cpp test/test_state_sp1/
git commit -m "feat(sp1): AppState deviceName/time + applyNameCmd/applyTime

Empty name rejected, long name truncated to 32 chars.
Time sync stored with millis() stamp for offset math later."
```

---

## Task 12: Wire dispatch in main.cpp + initialize deviceName + owner ack

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Initialize `deviceName` in `setup()`**

In `src/main.cpp::setup()`, after `initUi();` line, add:

```cpp
  appState.deviceName = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
```

- [ ] **Step 2: Add owner ack after existing `applyOwner`**

In `src/main.cpp::onLine`, find the existing `Owner` case:

```cpp
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      break;
```

Replace with:

```cpp
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      sendLine(formatAck("owner", true));
      break;
```

- [ ] **Step 3: Add new MessageKind dispatch cases**

In the same `switch (m.kind)` block, add the following cases (place after `Time`, before `Unknown`):

```cpp
    case MessageKind::StatusCmd: {
      StatusSnapshot snap = captureStatus(appState, now);
      sendLine(formatStatusAck(snap));
      break;
    }
    case MessageKind::NameCmd: {
      std::string err;
      bool ok = applyNameCmd(appState, m.nameValue, err);
      sendLine(formatAck("name", ok, err));
      break;
    }
    case MessageKind::UnpairCmd:
      sendLine(formatAck("unpair", true));
      break;
    case MessageKind::TurnEvent:
      // drop
      break;
```

- [ ] **Step 4: Handle Time case**

Replace the existing combined case label pattern:

```cpp
    case MessageKind::Time:
    case MessageKind::Unknown:
    case MessageKind::ParseError:
      break;
```

with:

```cpp
    case MessageKind::Time:
      applyTime(appState, m.timeEpoch, m.timeOffsetSec, now);
      break;
    case MessageKind::Unknown:
    case MessageKind::ParseError:
      break;
```

- [ ] **Step 5: Add required includes**

Ensure `src/main.cpp` top of file includes:

```cpp
#include "status.h"
```

(The other headers — protocol.h, state.h — are already included.)

- [ ] **Step 6: Verify device build**

Run:
```bash
pio run -e seeed_wio_terminal
```
Expected: compile succeeds. If link error about `captureStatus`, verify `status.cpp` compiles (not in `[env:native]` build, so the device env picks it up automatically via PlatformIO's default source discovery).

- [ ] **Step 7: Run all native tests one more time**

Run:
```bash
pio test -e native
```
Expected: all suites PASS.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(sp1): wire up status/name/unpair/turn/time dispatch in main

- initialize deviceName from DEVICE_NAME_PREFIX+suffix
- owner cmd now acks {ok:true}
- status cmd → captureStatus → formatStatusAck → sendLine
- name cmd → applyNameCmd → formatAck (ok or empty-name err)
- unpair cmd → formatAck ok (no bonds to clear in SP1)
- turn event dropped
- time event applied to AppState"
```

---

## Task 13: Device smoke test (manual)

**Files:**
- No code changes

- [ ] **Step 1: Upload and open serial monitor**

Run:
```bash
pio run -e seeed_wio_terminal -t upload
pio device monitor -e seeed_wio_terminal --baud 115200
```

- [ ] **Step 2: Connect from Bluetility or nRF Connect**

Open Bluetility on the Mac. Connect to `Claude-XXXX`. Enable notifications on NUS TX (`6e400003-...`). Prepare to write to NUS RX (`6e400002-...`).

- [ ] **Step 3: Send each test line (append `\n` to each write)**

For each line below, write to RX characteristic and observe TX notifications + serial log.

| Input | Expected notification | Expected serial |
|---|---|---|
| `{"cmd":"status"}` | `{"ack":"status","ok":true,"data":{...}}` with up/heap populated | — |
| `{"cmd":"name","name":"TestBuddy"}` | `{"ack":"name","ok":true}` | — |
| `{"cmd":"name","name":""}` | `{"ack":"name","ok":false,"error":"empty name"}` | — |
| `{"cmd":"unpair"}` | `{"ack":"unpair","ok":true}` | — |
| `{"cmd":"owner","name":"Tzangms"}` | `{"ack":"owner","ok":true}` | — |
| `{"evt":"turn","role":"assistant","content":[{"type":"text","text":"hi"}]}` | **no** notification (dropped) | — |
| `{"total":1,"running":1,"waiting":0,"msg":"busy","entries":["10:42 a","10:41 b"],"tokens":50000,"tokens_today":3000}` | no ack (heartbeats aren't acked); Wio screen flips to Idle/busy state | — |

- [ ] **Step 4: Send a heartbeat with a prompt and verify A/C buttons still work**

Write:
```
{"total":1,"running":0,"waiting":1,"msg":"approve: Bash","prompt":{"id":"req_test","tool":"Bash","hint":"echo hi"}}
```

Expected: Wio screen shows prompt. Press button A. On TX characteristic, expect:
```
{"cmd":"permission","id":"req_test","decision":"once"}
```

This validates MVP A hasn't regressed.

- [ ] **Step 5: If anything fails, debug before proceeding**

Check serial log for `[BLE]` messages, ParseError traces, or unknown cmd warnings. Fix forward.

- [ ] **Step 6: No commit needed (manual test, no code change)**

---

## Task 14: Hardware Buddy integration test

**Files:**
- No code changes

- [ ] **Step 1: Enable developer mode in Claude Desktop**

`Help → Troubleshooting → Enable Developer Mode`

- [ ] **Step 2: Open Hardware Buddy window**

`Developer → Open Hardware Buddy…` → Click **Connect** → select `Claude-XXXX`.

- [ ] **Step 3: Observe stats panel**

Expected:
- `name` shows `Claude-XXXX`
- `uptime` increments every second
- `heap` shows a plausible number (tens of thousands of bytes)
- `bat` shows 100% USB (fake values per spec design decision)
- `stats` shows appr=0, deny=0, etc. (no persistence yet)

- [ ] **Step 4: Trigger a permission prompt via real Claude activity**

Ask Claude to do something that triggers a permission (e.g. `Run ls` in Code). When the prompt appears, Wio should display it. Press A on the Wio. Claude should proceed with the action.

- [ ] **Step 5: Record pass/fail for each checkpoint**

If all five checkpoints pass, SP1 is complete. If any fail, open an issue and either fix or document as known limitation before merging.

- [ ] **Step 6: Final commit (if you want a marker)**

Optional, but helpful for history:

```bash
git commit --allow-empty -m "test(sp1): Hardware Buddy integration verified

- stats panel populated (name/up/heap/bat/stats)
- MVP A permission flow intact
- all native tests green"
```

- [ ] **Step 7: Push branch and open PR**

```bash
git push -u origin feature/sp1-protocol-completeness
gh pr create --title "SP1: protocol completeness" --body "$(cat <<'EOF'
## Summary
Implements SP1 of the 5-part Wio Terminal ↔ claude-desktop-buddy full-parity roadmap. Protocol layer only — no UI changes, no persistence, no security.

See `docs/superpowers/specs/2026-04-19-sp1-protocol-completeness-design.md`.

## Test plan
- [x] `pio test -e native` all green
- [x] Device smoke test with Bluetility (see plan Task 13)
- [x] Hardware Buddy window shows populated stats panel
- [x] MVP A approve/deny flow intact

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Self-Review

After writing the plan above, cross-checked against spec:

**Spec coverage check:**
- ✅ `status` cmd → Task 5 (parser) + Task 10 (formatter) + Task 12 (dispatch)
- ✅ `name` cmd → Task 6 (parser) + Task 11 (apply) + Task 12 (dispatch)
- ✅ `unpair` cmd → Task 5 (parser) + Task 12 (dispatch)
- ✅ `owner` cmd ack → Task 12 (dispatch replacement)
- ✅ `turn` event → Task 4 (parser) + Task 12 (drop)
- ✅ heartbeat `entries` → Task 7
- ✅ heartbeat `tokens`/`tokens_today` → Task 8
- ✅ `time` sync applied → Task 11 + Task 12
- ✅ Status ack bat/sys/stats strategy → Task 10 (formatter writes fake bat + real sys + zero stats)
- ✅ Ack system generic helper → Task 9
- ✅ `StatusSnapshot` + layered `status.cpp` → Task 10
- ✅ Device smoke test all 7 scenarios → Task 13
- ✅ Hardware Buddy integration → Task 14
- ✅ MVP A regression check → Task 13 Step 4
- ✅ R2 (free heap API) resolved → Task 1
- ✅ Q-OPEN-2 (MTU chunking) resolved → Task 2
- ✅ Native build filter includes status.cpp → Task 3

**Placeholder scan:** No TBD / TODO / "implement appropriate X" — all steps contain concrete code or exact commands.

**Type consistency check:**
- `HeartbeatData::entries` (vector<string>) referenced consistently across Tasks 7, 8, 13
- `HeartbeatData::tokens` / `tokens_today` (int64_t) referenced consistently Tasks 8, 13
- `AppState::deviceName` (string) referenced consistently Tasks 10, 11, 12
- `AppState::timeEpoch` (int64_t in spec — but I used that in task; check: Task 11 declares `int64_t timeEpoch = 0;` ✓)
- `applyNameCmd` signature `(AppState&, const string&, string& err)` returns bool — consistent across Tasks 11, 12
- `formatAck(cmd, ok, error="")` consistent across Tasks 9, 12
- `formatStatusAck(const StatusSnapshot&)` consistent across Tasks 10, 12
- `captureStatus(const AppState&, uint32_t)` consistent across Tasks 10, 12
- `MessageKind` enum matches across declarations and switch cases

**Open follow-ups (not in SP1 scope but noted):**
- Q-OPEN-3 (bat config extraction) — deferred to SP2 when real battery is added; not worth a task now
- R1 (onLine callback sync sendLine) — no observed issue in Task 2 spike would promote to task; document if Task 13 smoke test reveals hangs
