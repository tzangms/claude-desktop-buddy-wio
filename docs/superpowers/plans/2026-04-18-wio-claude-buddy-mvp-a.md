# Wio Terminal Claude Buddy MVP A — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an MVP Wio Terminal firmware that connects to Claude Desktop over BLE (Nordic UART Service), displays heartbeat data, and allows approve/deny of permission prompts via hardware buttons.

**Architecture:** PlatformIO project, Arduino framework on ATSAMD51. BLE via `Seeed_Arduino_rpcBLE` (RTL8720DN co-processor). Display via `Seeed_Arduino_LCD` (TFT_eSPI fork). Pure logic modules (`state`, `protocol`) use `std::string` so they compile on PlatformIO `native` env for Unity unit tests. Hardware modules (`ble_nus`, `ui`, `buttons`) tested by smoke sketches on real hardware.

**Tech Stack:**
- PlatformIO Core + board `seeed_wio_terminal`
- Arduino framework, `Seeed_Arduino_LCD`, `Seeed_Arduino_rpcBLE` + dependencies
- `ArduinoJson` v6 for wire protocol
- Unity (built-in) for native unit tests

**Design reference:** `docs/superpowers/specs/2026-04-18-wio-claude-buddy-design.md`

**Protocol reference:** <https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md>

---

## File Structure

Each file has a single, testable responsibility.

```
wioclaude/
├── .gitignore
├── platformio.ini                # board config + lib_deps + native test env
├── README.md                     # setup, RTL8720 firmware update, flash steps
├── src/
│   ├── main.cpp                  # setup/loop, glue all modules together
│   ├── config.h                  # UUIDs, timeouts, colors, pin constants
│   ├── state.h                   # AppState struct, Mode enum, transition fns (pure)
│   ├── state.cpp
│   ├── protocol.h                # JSON parse/serialize (pure, std::string)
│   ├── protocol.cpp
│   ├── ble_nus.h                 # rpcBLE NUS wrapper: init, advertise, RX, TX
│   ├── ble_nus.cpp
│   ├── ui.h                      # initUi, renderIdle, renderPrompt, renderDisconnected
│   ├── ui.cpp
│   ├── buttons.h                 # initButtons, pollButtons → ButtonEvent
│   └── buttons.cpp
└── test/
    ├── test_protocol/
    │   └── test_protocol.cpp     # Unity tests for parse/serialize
    └── test_state/
        └── test_state.cpp        # Unity tests for state transitions
```

**Module dependency rule:**
- `state` depends on nothing
- `protocol` depends on `state` (it writes parsed data into AppState)
- `ble_nus`, `ui`, `buttons` depend on Arduino framework + hardware libs
- `main.cpp` orchestrates everything

---

## Task 1: Project Scaffolding + Hello World

**Files:**
- Create: `.gitignore`
- Create: `platformio.ini`
- Create: `src/main.cpp`

- [ ] **Step 1.1: Create `.gitignore`**

```
.pio/
.vscode/
.DS_Store
*.swp
```

- [ ] **Step 1.2: Create `platformio.ini`**

```ini
[env:seeed_wio_terminal]
platform = atmelsam
board = seeed_wio_terminal
framework = arduino
monitor_speed = 115200
build_flags =
    -std=gnu++17
    -DLCD_BACKLIGHT=72
lib_deps =
    bblanchon/ArduinoJson@^6.21.5
    seeedstudio/Seeed Arduino FS@^2.1.1
    seeedstudio/Seeed Arduino SFUD@^2.0.2
    seeedstudio/Seeed Arduino rpcUnified@^2.1.4
    seeedstudio/Seeed Arduino mbedtls@^3.0.1
    seeedstudio/Seeed Arduino rpcBLE@^1.0.1
    seeedstudio/Seeed_Arduino_LCD@^1.0.0

[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -I src
lib_deps =
    bblanchon/ArduinoJson@^6.21.5
```

Note: lib names on PlatformIO registry are case-sensitive — if `pio pkg install` fails on a name, search with `pio pkg search "rpcBLE"` and use the exact id returned. The versions above are minimum known-working as of the design date; newer patches usually work.

- [ ] **Step 1.3: Create minimal `src/main.cpp` (LCD hello world)**

```cpp
#include <Arduino.h>
#include "TFT_eSPI.h"

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
  tft.begin();
  tft.setRotation(3);  // landscape, USB-C on left
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Claude Buddy boot OK");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}
```

- [ ] **Step 1.4: Compile (without upload)**

Run: `pio run -e seeed_wio_terminal`
Expected: build completes. If `rpcBLE` / deps fail to install, resolve name/version before proceeding — do not skip.

- [ ] **Step 1.5: Upload and verify**

Plug Wio Terminal in, flip power switch down (left pin header side), then **double-tap** the power switch to put it in bootloader mode (green LED pulses).

Run: `pio run -e seeed_wio_terminal -t upload`
Then: `pio device monitor -b 115200`

Expected: screen shows **"Claude Buddy boot OK"**, serial prints `alive` once per second.

If screen is blank, double-check `setRotation(3)` and backlight. If upload fails, redo the bootloader double-tap.

- [ ] **Step 1.6: Commit**

```bash
git add .gitignore platformio.ini src/main.cpp
git commit -m "chore: scaffold PlatformIO project with LCD hello world"
```

---

## Task 2: Config Header

**Files:**
- Create: `src/config.h`

- [ ] **Step 2.1: Create `src/config.h`**

```cpp
#pragma once

#include <cstdint>

// --- BLE ---
static constexpr const char* NUS_SERVICE_UUID =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char* NUS_RX_UUID =
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char* NUS_TX_UUID =
    "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static constexpr const char* DEVICE_NAME_PREFIX = "Claude-Wio-";

// --- Timeouts (ms) ---
static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 30000;
static constexpr uint32_t BACKLIGHT_IDLE_MS    = 30000;
static constexpr uint32_t ACK_DISPLAY_MS       = 1000;
static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 20;
static constexpr uint32_t POST_SEND_LOCKOUT_MS = 500;

// --- Display (320x240 landscape after setRotation(3)) ---
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;

// --- Colors (RGB565) ---
static constexpr uint16_t COLOR_BG         = 0x0000;  // black
static constexpr uint16_t COLOR_FG         = 0xFFFF;  // white
static constexpr uint16_t COLOR_DIM        = 0x7BEF;  // grey
static constexpr uint16_t COLOR_OK         = 0x07E0;  // green
static constexpr uint16_t COLOR_WARN       = 0xFD20;  // orange
static constexpr uint16_t COLOR_ALERT_BG   = 0xC000;  // red
static constexpr uint16_t COLOR_ALERT_TEXT = 0xFFFF;

// --- Backlight ---
static constexpr uint8_t BACKLIGHT_FULL = 255;
static constexpr uint8_t BACKLIGHT_DIM  = 50;
```

- [ ] **Step 2.2: Compile to verify syntax**

Run: `pio run -e seeed_wio_terminal`
Expected: build passes (config.h isn't included yet but must be syntactically valid).

- [ ] **Step 2.3: Commit**

```bash
git add src/config.h
git commit -m "feat: add config.h with UUIDs, timeouts, colors"
```

---

## Task 3: State Module (TDD)

**Files:**
- Create: `src/state.h`
- Create: `src/state.cpp`
- Create: `test/test_state/test_state.cpp`

- [ ] **Step 3.1: Create header `src/state.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

enum class Mode {
  BleInit,
  Advertising,
  Connected,     // connected but no heartbeat yet
  Idle,
  Prompt,
  Ack,
  Disconnected,
  Fatal,
};

struct PromptData {
  std::string id;
  std::string tool;
  std::string hint;
};

struct HeartbeatData {
  int total = 0;
  int running = 0;
  int waiting = 0;
  std::string msg;
  bool hasPrompt = false;
  PromptData prompt;
};

struct AppState {
  Mode mode = Mode::BleInit;
  HeartbeatData hb;
  std::string ownerName;
  uint32_t lastHeartbeatMs = 0;
  // ACK display state
  bool ackApproved = false;
  uint32_t ackUntilMs = 0;
};

enum class PermissionDecision { Approve, Deny };

// Pure transition functions. Return true if the state changed in a way
// that requires a re-render.
bool applyHeartbeat(AppState& s, const HeartbeatData& hb, uint32_t nowMs);
bool applyOwner(AppState& s, const std::string& name);
bool applyDisconnect(AppState& s);
bool applyConnected(AppState& s);

// Returns true if a decision should be sent. `out` is set to the decision.
bool applyButton(AppState& s, char button, uint32_t nowMs,
                 PermissionDecision& out, std::string& outPromptId);

// Returns true if mode changed. Call every loop.
bool applyTimeouts(AppState& s, uint32_t nowMs);
```

- [ ] **Step 3.2: Write the first failing test (`test/test_state/test_state.cpp`)**

```cpp
#include <unity.h>
#include "state.h"

static AppState freshConnected() {
  AppState s;
  s.mode = Mode::Connected;
  return s;
}

void test_first_heartbeat_without_prompt_enters_idle() {
  AppState s = freshConnected();
  HeartbeatData hb;
  hb.total = 3; hb.running = 1; hb.waiting = 0;
  hb.msg = "working";
  bool changed = applyHeartbeat(s, hb, 1000);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL(3, s.hb.total);
  TEST_ASSERT_EQUAL_UINT32(1000, s.lastHeartbeatMs);
}

void test_heartbeat_with_prompt_enters_prompt() {
  AppState s = freshConnected();
  HeartbeatData hb;
  hb.hasPrompt = true;
  hb.prompt.id = "req_abc";
  hb.prompt.tool = "Bash";
  hb.prompt.hint = "rm -rf /tmp/foo";
  applyHeartbeat(s, hb, 500);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Prompt), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL_STRING("req_abc", s.hb.prompt.id.c_str());
}

void test_prompt_cleared_by_heartbeat_returns_to_idle() {
  AppState s = freshConnected();
  HeartbeatData h1; h1.hasPrompt = true; h1.prompt.id = "req_abc";
  applyHeartbeat(s, h1, 100);
  HeartbeatData h2; h2.hasPrompt = false;
  applyHeartbeat(s, h2, 200);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
}

void test_button_a_in_prompt_requests_approve() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_xyz";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  bool sent = applyButton(s, 'A', 100, d, id);
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_EQUAL(static_cast<int>(PermissionDecision::Approve),
                    static_cast<int>(d));
  TEST_ASSERT_EQUAL_STRING("req_xyz", id.c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Ack), static_cast<int>(s.mode));
  TEST_ASSERT_TRUE(s.ackApproved);
}

void test_button_c_in_prompt_requests_deny() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_xyz";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  bool sent = applyButton(s, 'C', 100, d, id);
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_EQUAL(static_cast<int>(PermissionDecision::Deny),
                    static_cast<int>(d));
  TEST_ASSERT_FALSE(s.ackApproved);
}

void test_button_in_idle_does_nothing() {
  AppState s = freshConnected();
  HeartbeatData hb; applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  TEST_ASSERT_FALSE(applyButton(s, 'A', 100, d, id));
}

void test_ack_expires_to_idle() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_1";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  applyButton(s, 'A', 100, d, id);
  // Not expired yet
  TEST_ASSERT_FALSE(applyTimeouts(s, 500));
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Ack), static_cast<int>(s.mode));
  // Expired (ACK_DISPLAY_MS = 1000)
  TEST_ASSERT_TRUE(applyTimeouts(s, 1200));
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
}

void test_heartbeat_timeout_disconnects() {
  AppState s = freshConnected();
  HeartbeatData hb; applyHeartbeat(s, hb, 0);
  TEST_ASSERT_FALSE(applyTimeouts(s, 20000));
  TEST_ASSERT_TRUE(applyTimeouts(s, 40000));  // > HEARTBEAT_TIMEOUT_MS
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Disconnected),
                    static_cast<int>(s.mode));
}

void test_new_prompt_id_during_prompt_updates_without_ack() {
  AppState s = freshConnected();
  HeartbeatData h1; h1.hasPrompt = true; h1.prompt.id = "req_1"; h1.prompt.tool = "Bash";
  applyHeartbeat(s, h1, 0);
  HeartbeatData h2; h2.hasPrompt = true; h2.prompt.id = "req_2"; h2.prompt.tool = "Read";
  applyHeartbeat(s, h2, 100);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Prompt), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL_STRING("req_2", s.hb.prompt.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Read", s.hb.prompt.tool.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_heartbeat_without_prompt_enters_idle);
  RUN_TEST(test_heartbeat_with_prompt_enters_prompt);
  RUN_TEST(test_prompt_cleared_by_heartbeat_returns_to_idle);
  RUN_TEST(test_button_a_in_prompt_requests_approve);
  RUN_TEST(test_button_c_in_prompt_requests_deny);
  RUN_TEST(test_button_in_idle_does_nothing);
  RUN_TEST(test_ack_expires_to_idle);
  RUN_TEST(test_heartbeat_timeout_disconnects);
  RUN_TEST(test_new_prompt_id_during_prompt_updates_without_ack);
  return UNITY_END();
}
```

- [ ] **Step 3.3: Run tests — they must fail (no implementation yet)**

Run: `pio test -e native`
Expected: **compile error** — `state.h` has no `.cpp` implementation, link fails. That's the "red" in red-green-refactor.

- [ ] **Step 3.4: Implement `src/state.cpp`**

```cpp
#include "state.h"
#include "config.h"

bool applyHeartbeat(AppState& s, const HeartbeatData& hb, uint32_t nowMs) {
  Mode prev = s.mode;
  std::string prevPromptId = s.hb.prompt.id;
  s.hb = hb;
  s.lastHeartbeatMs = nowMs;

  // Re-entry from Disconnected / Advertising / Connected → Idle or Prompt
  if (hb.hasPrompt) {
    s.mode = Mode::Prompt;
  } else {
    // Don't clobber Ack with Idle mid-animation — let applyTimeouts handle it
    if (s.mode != Mode::Ack) {
      s.mode = Mode::Idle;
    }
  }
  return s.mode != prev || s.hb.prompt.id != prevPromptId;
}

bool applyOwner(AppState& s, const std::string& name) {
  if (s.ownerName == name) return false;
  s.ownerName = name;
  return true;
}

bool applyDisconnect(AppState& s) {
  if (s.mode == Mode::Disconnected) return false;
  s.mode = Mode::Disconnected;
  return true;
}

bool applyConnected(AppState& s) {
  if (s.mode == Mode::Connected) return false;
  s.mode = Mode::Connected;
  return true;
}

bool applyButton(AppState& s, char button, uint32_t nowMs,
                 PermissionDecision& out, std::string& outPromptId) {
  if (s.mode != Mode::Prompt) return false;
  if (button != 'A' && button != 'C') return false;
  out = (button == 'A') ? PermissionDecision::Approve
                        : PermissionDecision::Deny;
  outPromptId = s.hb.prompt.id;
  s.ackApproved = (button == 'A');
  s.ackUntilMs = nowMs + ACK_DISPLAY_MS;
  s.mode = Mode::Ack;
  return true;
}

bool applyTimeouts(AppState& s, uint32_t nowMs) {
  // ACK expiry → Idle
  if (s.mode == Mode::Ack && nowMs >= s.ackUntilMs) {
    s.mode = s.hb.hasPrompt ? Mode::Prompt : Mode::Idle;
    return true;
  }
  // Heartbeat timeout → Disconnected (only from live modes)
  bool live = (s.mode == Mode::Idle || s.mode == Mode::Prompt ||
               s.mode == Mode::Ack);
  if (live && s.lastHeartbeatMs != 0 &&
      (nowMs - s.lastHeartbeatMs) > HEARTBEAT_TIMEOUT_MS) {
    s.mode = Mode::Disconnected;
    return true;
  }
  return false;
}
```

- [ ] **Step 3.5: Run tests — they must pass**

Run: `pio test -e native`
Expected: **all 9 tests PASS**. If any fail, fix the implementation (not the test) and re-run.

- [ ] **Step 3.6: Verify firmware build still compiles**

Run: `pio run -e seeed_wio_terminal`
Expected: build passes (state.cpp is not yet included in main.cpp but must compile).

- [ ] **Step 3.7: Commit**

```bash
git add src/state.h src/state.cpp test/test_state/test_state.cpp
git commit -m "feat: state module with transition functions (TDD)"
```

---

## Task 4: Protocol Module (TDD)

**Files:**
- Create: `src/protocol.h`
- Create: `src/protocol.cpp`
- Create: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 4.1: Create header `src/protocol.h`**

```cpp
#pragma once

#include <string>
#include "state.h"

enum class MessageKind {
  Heartbeat,
  Owner,
  Time,
  Unknown,  // includes "evt":"turn" and anything else we ignore
  ParseError,
};

struct ParsedMessage {
  MessageKind kind = MessageKind::Unknown;
  HeartbeatData heartbeat;
  std::string ownerName;
  int32_t timeEpoch = 0;
  int32_t timeOffsetSec = 0;
};

// Parse one line of JSON (already stripped of trailing '\n').
ParsedMessage parseLine(const std::string& line);

// Build a permission-decision JSON line terminated with '\n'.
std::string formatPermission(const std::string& promptId,
                             PermissionDecision decision);
```

- [ ] **Step 4.2: Write failing tests (`test/test_protocol/test_protocol.cpp`)**

```cpp
#include <unity.h>
#include "protocol.h"

void test_parse_heartbeat_basic() {
  std::string line = R"({"total":3,"running":1,"waiting":0,"msg":"working"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL(3, m.heartbeat.total);
  TEST_ASSERT_EQUAL(1, m.heartbeat.running);
  TEST_ASSERT_EQUAL_STRING("working", m.heartbeat.msg.c_str());
  TEST_ASSERT_FALSE(m.heartbeat.hasPrompt);
}

void test_parse_heartbeat_with_prompt() {
  std::string line = R"({"total":1,"running":0,"waiting":1,"msg":"approve: Bash","prompt":{"id":"req_abc","tool":"Bash","hint":"rm -rf /tmp/foo"}})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_TRUE(m.heartbeat.hasPrompt);
  TEST_ASSERT_EQUAL_STRING("req_abc", m.heartbeat.prompt.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Bash", m.heartbeat.prompt.tool.c_str());
  TEST_ASSERT_EQUAL_STRING("rm -rf /tmp/foo", m.heartbeat.prompt.hint.c_str());
}

void test_parse_owner() {
  std::string line = R"({"cmd":"owner","name":"Felix"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Owner),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("Felix", m.ownerName.c_str());
}

void test_parse_time() {
  std::string line = R"({"time":[1775731234,-25200]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Time),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_INT32(1775731234, m.timeEpoch);
  TEST_ASSERT_EQUAL_INT32(-25200, m.timeOffsetSec);
}

void test_parse_turn_event_is_unknown() {
  std::string line = R"({"evt":"turn","role":"assistant","content":[]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown),
                    static_cast<int>(m.kind));
}

void test_parse_malformed_is_error() {
  std::string line = R"({not json)";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::ParseError),
                    static_cast<int>(m.kind));
}

void test_format_permission_approve() {
  std::string out = formatPermission("req_abc", PermissionDecision::Approve);
  TEST_ASSERT_EQUAL_STRING(
      R"({"cmd":"permission","id":"req_abc","decision":"once"})" "\n",
      out.c_str());
}

void test_format_permission_deny() {
  std::string out = formatPermission("req_xyz", PermissionDecision::Deny);
  TEST_ASSERT_EQUAL_STRING(
      R"({"cmd":"permission","id":"req_xyz","decision":"deny"})" "\n",
      out.c_str());
}

void test_parse_heartbeat_missing_optional_fields() {
  std::string line = R"({"total":0,"running":0,"waiting":0})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("", m.heartbeat.msg.c_str());
  TEST_ASSERT_FALSE(m.heartbeat.hasPrompt);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_heartbeat_basic);
  RUN_TEST(test_parse_heartbeat_with_prompt);
  RUN_TEST(test_parse_owner);
  RUN_TEST(test_parse_time);
  RUN_TEST(test_parse_turn_event_is_unknown);
  RUN_TEST(test_parse_malformed_is_error);
  RUN_TEST(test_format_permission_approve);
  RUN_TEST(test_format_permission_deny);
  RUN_TEST(test_parse_heartbeat_missing_optional_fields);
  return UNITY_END();
}
```

- [ ] **Step 4.3: Run tests — must fail**

Run: `pio test -e native -f test_protocol`
Expected: link error (no `parseLine` / `formatPermission` implementations yet).

- [ ] **Step 4.4: Implement `src/protocol.cpp`**

```cpp
#include "protocol.h"
#include <ArduinoJson.h>

ParsedMessage parseLine(const std::string& line) {
  ParsedMessage m;
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    m.kind = MessageKind::ParseError;
    return m;
  }

  // Heartbeat: has "total"
  if (doc.containsKey("total")) {
    m.kind = MessageKind::Heartbeat;
    m.heartbeat.total   = doc["total"]   | 0;
    m.heartbeat.running = doc["running"] | 0;
    m.heartbeat.waiting = doc["waiting"] | 0;
    const char* msg = doc["msg"] | "";
    m.heartbeat.msg = msg;
    if (doc.containsKey("prompt") && !doc["prompt"].isNull()) {
      m.heartbeat.hasPrompt = true;
      m.heartbeat.prompt.id   = doc["prompt"]["id"]   | "";
      m.heartbeat.prompt.tool = doc["prompt"]["tool"] | "";
      m.heartbeat.prompt.hint = doc["prompt"]["hint"] | "";
    }
    return m;
  }

  // Owner
  if (doc["cmd"] == "owner") {
    m.kind = MessageKind::Owner;
    m.ownerName = doc["name"] | "";
    return m;
  }

  // Time
  if (doc.containsKey("time") && doc["time"].is<JsonArray>()) {
    JsonArray a = doc["time"].as<JsonArray>();
    if (a.size() >= 2) {
      m.kind = MessageKind::Time;
      m.timeEpoch     = a[0] | 0;
      m.timeOffsetSec = a[1] | 0;
      return m;
    }
  }

  m.kind = MessageKind::Unknown;
  return m;
}

std::string formatPermission(const std::string& promptId,
                             PermissionDecision decision) {
  StaticJsonDocument<256> doc;
  doc["cmd"] = "permission";
  doc["id"] = promptId;
  doc["decision"] = (decision == PermissionDecision::Approve) ? "once" : "deny";
  std::string out;
  serializeJson(doc, out);
  out += '\n';
  return out;
}
```

- [ ] **Step 4.5: Run tests — must pass**

Run: `pio test -e native -f test_protocol`
Expected: **all 9 tests PASS**.

- [ ] **Step 4.6: Run all native tests to confirm state tests still pass**

Run: `pio test -e native`
Expected: **18 tests PASS** (9 state + 9 protocol).

- [ ] **Step 4.7: Commit**

```bash
git add src/protocol.h src/protocol.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat: protocol module with JSON parse/serialize (TDD)"
```

---

## Task 5: UI Module (Hardware Smoke Test)

**Files:**
- Create: `src/ui.h`
- Create: `src/ui.cpp`
- Modify: `src/main.cpp` (temporary smoke test cycling screens)

- [ ] **Step 5.1: Create `src/ui.h`**

```cpp
#pragma once

#include "state.h"

void initUi();
void renderBoot(const char* msg);
void renderAdvertising(const char* deviceName);
void renderConnected();
void renderIdle(const AppState& s);
void renderPrompt(const AppState& s);
void renderAck(const AppState& s);
void renderDisconnected();
void renderFatal(const char* msg);
void setBacklight(uint8_t pct);  // 0–100
```

- [ ] **Step 5.2: Implement `src/ui.cpp`**

```cpp
#include "ui.h"
#include "config.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;

static void clearAll() {
  tft.fillScreen(COLOR_BG);
}

static void drawHeader(const char* title, uint16_t bg, uint16_t fg) {
  tft.fillRect(0, 0, SCREEN_W, 28, bg);
  tft.setTextColor(fg, bg);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print(title);
}

static void drawFooter(const char* text) {
  tft.fillRect(0, SCREEN_H - 22, SCREEN_W, 22, COLOR_BG);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(8, SCREEN_H - 16);
  tft.print(text);
}

void initUi() {
  pinMode(LCD_BACKLIGHT, OUTPUT);
  analogWrite(LCD_BACKLIGHT, BACKLIGHT_FULL);
  tft.begin();
  tft.setRotation(3);
  clearAll();
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  analogWrite(LCD_BACKLIGHT, (uint8_t)((uint16_t)BACKLIGHT_FULL * pct / 100));
}

void renderBoot(const char* msg) {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.print(msg);
}

void renderAdvertising(const char* deviceName) {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 90);
  tft.print("advertising as");
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.print(deviceName);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 150);
  tft.print("open Claude Desktop Dev menu to pair");
}

void renderConnected() {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_OK, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(40, 100);
  tft.print("connected, waiting...");
}

void renderIdle(const AppState& s) {
  clearAll();

  // Header with status dot
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.fillCircle(SCREEN_W - 20, 14, 5, COLOR_OK);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(SCREEN_W - 100, 10);
  tft.print("connected");

  // Labels
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(28, 50);   tft.print("Total");
  tft.setCursor(130, 50);  tft.print("Running");
  tft.setCursor(240, 50);  tft.print("Waiting");

  // Big numbers
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(5);
  auto drawNum = [](int x, int n) {
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 70); tft.print(buf);
  };
  drawNum(38,  s.hb.total);
  drawNum(148, s.hb.running);
  drawNum(258, s.hb.waiting);

  // msg line
  tft.fillRect(0, 160, SCREEN_W, 20, COLOR_BG);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(8, 165);
  tft.print(s.hb.msg.c_str());

  // footer: Hi, <owner>
  if (!s.ownerName.empty()) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Hi, %s", s.ownerName.c_str());
    drawFooter(buf);
  } else {
    drawFooter("");
  }
}

void renderPrompt(const AppState& s) {
  clearAll();
  drawHeader("! PERMISSION REQUESTED", COLOR_ALERT_BG, COLOR_ALERT_TEXT);

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 50);
  tft.print("Tool");

  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(16, 65);
  tft.print(s.hb.prompt.tool.c_str());

  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  // naive truncation to ~26 chars
  std::string hint = s.hb.prompt.hint;
  if (hint.size() > 26) { hint.resize(25); hint += "\xE2\x80\xA6"; }
  tft.setCursor(16, 120);
  tft.print(hint.c_str());

  // Button hints
  tft.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, 0x2104);
  tft.setTextColor(COLOR_FG);
  tft.setTextSize(2);
  tft.setCursor(12, SCREEN_H - 20);
  tft.print("[C] Deny");
  tft.setCursor(SCREEN_W - 170, SCREEN_H - 20);
  tft.print("[A] Allow once");
}

void renderAck(const AppState& s) {
  clearAll();
  const char* txt = s.ackApproved ? "Approved" : "Denied";
  uint16_t color  = s.ackApproved ? COLOR_OK : COLOR_ALERT_BG;
  tft.setTextColor(color, COLOR_BG);
  tft.setTextSize(5);
  int16_t w = (int16_t)strlen(txt) * 6 * 5;
  tft.setCursor((SCREEN_W - w) / 2, 100);
  tft.print(txt);
}

void renderDisconnected() {
  clearAll();
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(40, 90);
  tft.print("Disconnected");
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(40, 140);
  tft.print("scanning...");
}

void renderFatal(const char* msg) {
  clearAll();
  drawHeader("FATAL", COLOR_ALERT_BG, COLOR_ALERT_TEXT);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 80);
  tft.print(msg);
}
```

- [ ] **Step 5.3: Replace `src/main.cpp` with a UI smoke test**

```cpp
#include <Arduino.h>
#include "ui.h"
#include "state.h"

static int stage = 0;
static uint32_t lastSwitch = 0;

static AppState makeIdle() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.total = 3; s.hb.running = 1; s.hb.waiting = 0;
  s.hb.msg = "working on login flow...";
  s.ownerName = "Felix";
  return s;
}

static AppState makePrompt() {
  AppState s;
  s.mode = Mode::Prompt;
  s.hb.hasPrompt = true;
  s.hb.prompt.tool = "Bash";
  s.hb.prompt.hint = "rm -rf /tmp/foo";
  return s;
}

static AppState makeAck(bool approved) {
  AppState s;
  s.mode = Mode::Ack;
  s.ackApproved = approved;
  return s;
}

void setup() {
  Serial.begin(115200);
  initUi();
  renderBoot("UI smoke test");
  delay(800);
}

void loop() {
  uint32_t now = millis();
  if (now - lastSwitch > 2500) {
    lastSwitch = now;
    switch (stage) {
      case 0: renderAdvertising("Claude-Wio-AB12"); break;
      case 1: renderConnected(); break;
      case 2: renderIdle(makeIdle()); break;
      case 3: renderPrompt(makePrompt()); break;
      case 4: renderAck(makeAck(true)); break;
      case 5: renderAck(makeAck(false)); break;
      case 6: renderDisconnected(); break;
      case 7: renderFatal("BLE init failed"); break;
    }
    stage = (stage + 1) % 8;
  }
}
```

- [ ] **Step 5.4: Flash and visually inspect each screen**

Run: `pio run -e seeed_wio_terminal -t upload && pio device monitor -b 115200`

Expected: screen cycles through 8 stages every 2.5s. Verify by eye:
- Boot → Advertising → Connected → Idle (numbers visible, "Hi, Felix") → Prompt (red header, Bash tool) → Ack Approved (green) → Ack Denied (red) → Disconnected → Fatal

If any screen is cut off, the wrong rotation, or colors wrong: fix `ui.cpp` and re-flash. Do NOT proceed until every screen looks right.

- [ ] **Step 5.5: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "feat: ui module with all MVP screens (smoke-tested)"
```

---

## Task 6: Buttons Module (Hardware Smoke Test)

**Files:**
- Create: `src/buttons.h`
- Create: `src/buttons.cpp`
- Modify: `src/main.cpp` (temporary button test)

- [ ] **Step 6.1: Create `src/buttons.h`**

```cpp
#pragma once
#include <cstdint>

enum class ButtonEvent {
  None,
  PressA,
  PressB,
  PressC,
  PressNav,  // 5-way center press (wakeup only)
};

void initButtons();
ButtonEvent pollButtons(uint32_t nowMs);
```

- [ ] **Step 6.2: Implement `src/buttons.cpp`**

```cpp
#include "buttons.h"
#include "config.h"
#include <Arduino.h>

struct Btn {
  uint8_t pin;
  bool lastRaw;
  bool stable;
  uint32_t lastChangeMs;
  ButtonEvent evt;
};

static Btn btns[] = {
  {WIO_KEY_A,    true, true, 0, ButtonEvent::PressA},
  {WIO_KEY_B,    true, true, 0, ButtonEvent::PressB},
  {WIO_KEY_C,    true, true, 0, ButtonEvent::PressC},
  {WIO_5S_PRESS, true, true, 0, ButtonEvent::PressNav},
};

void initButtons() {
  for (auto& b : btns) {
    pinMode(b.pin, INPUT_PULLUP);
    b.lastRaw = digitalRead(b.pin);
    b.stable = b.lastRaw;
  }
}

ButtonEvent pollButtons(uint32_t nowMs) {
  for (auto& b : btns) {
    bool raw = digitalRead(b.pin);
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChangeMs = nowMs;
    }
    if ((nowMs - b.lastChangeMs) >= BUTTON_DEBOUNCE_MS && raw != b.stable) {
      b.stable = raw;
      // Active-low: press = LOW
      if (raw == LOW) return b.evt;
    }
  }
  return ButtonEvent::None;
}
```

- [ ] **Step 6.3: Replace `src/main.cpp` with button smoke test**

```cpp
#include <Arduino.h>
#include "ui.h"
#include "buttons.h"

void setup() {
  Serial.begin(115200);
  initUi();
  initButtons();
  renderBoot("Button test — press A/B/C/Nav");
}

void loop() {
  uint32_t now = millis();
  ButtonEvent e = pollButtons(now);
  if (e != ButtonEvent::None) {
    const char* name = "?";
    switch (e) {
      case ButtonEvent::PressA:   name = "A pressed";   break;
      case ButtonEvent::PressB:   name = "B pressed";   break;
      case ButtonEvent::PressC:   name = "C pressed";   break;
      case ButtonEvent::PressNav: name = "Nav pressed"; break;
      default: break;
    }
    Serial.println(name);
    renderBoot(name);
  }
}
```

- [ ] **Step 6.4: Flash and press each button**

Run: `pio run -e seeed_wio_terminal -t upload && pio device monitor -b 115200`

Expected: pressing each top button shows `A/B/C pressed` on screen and serial; pressing the 5-way center shows `Nav pressed`. No ghost triggers when not pressed. No repeat events while held.

If button labels are swapped (e.g. what you expect to be A reports as C), adjust the `btns[]` initializer to match your physical layout (button labels are silk-screened on bottom of the Wio Terminal case — look there).

- [ ] **Step 6.5: Commit**

```bash
git add src/buttons.h src/buttons.cpp src/main.cpp
git commit -m "feat: buttons module with debounce (smoke-tested)"
```

---

## Task 7: BLE NUS Module

**Files:**
- Create: `src/ble_nus.h`
- Create: `src/ble_nus.cpp`
- Modify: `src/main.cpp` (temporary BLE echo test)

- [ ] **Step 7.1: Create `src/ble_nus.h`**

```cpp
#pragma once
#include <string>
#include <functional>

// Called for every complete line received (without the trailing '\n').
using LineCallback = std::function<void(const std::string&)>;

bool initBle(const std::string& nameSuffix, LineCallback onLine);
void pollBle();           // must be called regularly in loop()
bool isBleConnected();
bool sendLine(const std::string& line);  // line should include trailing '\n'
```

- [ ] **Step 7.2: Implement `src/ble_nus.cpp`**

```cpp
#include "ble_nus.h"
#include "config.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>

static BLECharacteristic* txChar = nullptr;
static BLECharacteristic* rxChar = nullptr;
static LineCallback onLineCb;
static std::string rxBuf;
static bool connected = false;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) override    { connected = true;  }
  void onDisconnect(BLEServer* s) override {
    connected = false;
    rxBuf.clear();
    s->getAdvertising()->start();
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    std::string v = c->getValue();
    rxBuf.append(v);
    size_t pos;
    while ((pos = rxBuf.find('\n')) != std::string::npos) {
      std::string line = rxBuf.substr(0, pos);
      rxBuf.erase(0, pos + 1);
      if (onLineCb) onLineCb(line);
    }
    // Safety: drop buffer if it grows unreasonably
    if (rxBuf.size() > 8192) rxBuf.clear();
  }
};

bool initBle(const std::string& nameSuffix, LineCallback onLine) {
  onLineCb = onLine;
  std::string name = std::string(DEVICE_NAME_PREFIX) + nameSuffix;
  BLEDevice::init(name);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService* svc = server->createService(NUS_SERVICE_UUID);

  txChar = svc->createCharacteristic(
      NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCB());

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.print("BLE advertising as: "); Serial.println(name.c_str());
  return true;
}

void pollBle() {
  // rpcBLE is event-driven; nothing to poll.
}

bool isBleConnected() { return connected; }

bool sendLine(const std::string& line) {
  if (!connected || !txChar) return false;
  txChar->setValue((uint8_t*)line.data(), line.size());
  txChar->notify();
  return true;
}
```

> **Note:** `Seeed_Arduino_rpcBLE` mirrors the ESP32 BLE API (`BLEDevice.h`, `BLEServer.h`, etc.). If the headers above don't resolve, search the installed lib path (`.pio/libdeps/seeed_wio_terminal/Seeed_Arduino_rpcBLE/src/`) and adjust includes to match. Core API (createServer/createCharacteristic/notify) is stable.

- [ ] **Step 7.3: Replace `src/main.cpp` with BLE echo smoke test**

```cpp
#include <Arduino.h>
#include "ui.h"
#include "ble_nus.h"

static void onLine(const std::string& line) {
  Serial.print("RX: "); Serial.println(line.c_str());
  // Echo back so we can verify TX works too
  sendLine(line + "\n");
}

void setup() {
  Serial.begin(115200);
  initUi();
  renderBoot("BLE init...");
  if (!initBle("TEST", onLine)) {
    renderFatal("BLE init failed");
    while (1) delay(1000);
  }
  renderAdvertising("Claude-Wio-TEST");
}

void loop() {
  pollBle();
  static bool wasConn = false;
  bool nowConn = isBleConnected();
  if (nowConn && !wasConn) renderConnected();
  if (!nowConn && wasConn)  renderAdvertising("Claude-Wio-TEST");
  wasConn = nowConn;
  delay(20);
}
```

- [ ] **Step 7.4: Flash, then test BLE with a generic tool**

Use **nRF Connect** (iOS/Android) or macOS's `blueutil` / LightBlue app:

1. Scan for `Claude-Wio-TEST`
2. Connect
3. Wio Terminal screen should change to "connected, waiting..."
4. Enable notifications on TX characteristic (`6e400003-...`)
5. Write bytes `68 65 6C 6C 6F 0A` (`hello\n`) to RX characteristic (`6e400002-...`)
6. Expect the TX characteristic notifies back `hello\n`
7. Serial monitor should print `RX: hello`

If connection drops immediately: the RTL8720DN firmware is likely too old. **Stop and update firmware** per README — do not proceed.

- [ ] **Step 7.5: Commit**

```bash
git add src/ble_nus.h src/ble_nus.cpp src/main.cpp
git commit -m "feat: ble_nus module wrapping rpcBLE NUS (smoke-tested)"
```

---

## Task 8: Wire Everything in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 8.1: Replace `src/main.cpp` with full MVP wiring**

```cpp
#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "protocol.h"
#include "ui.h"
#include "buttons.h"
#include "ble_nus.h"

static AppState appState;
static Mode lastRenderedMode = Mode::BleInit;
static std::string lastRenderedPromptId;
static uint32_t lastButtonSendMs = 0;
static uint32_t lastInteractionMs = 0;

static std::string deviceSuffix() {
  // Use MCU unique ID tail for 4-hex suffix (stable across reboots).
  uint32_t id = *(volatile uint32_t*)0x008061FC;  // SAMD51 serial word 3
  char buf[5];
  snprintf(buf, sizeof(buf), "%04X", (unsigned)(id & 0xFFFF));
  return std::string(buf);
}

static void markInteraction(uint32_t now) {
  lastInteractionMs = now;
  setBacklight(100);
}

static void render(bool force) {
  bool modeChanged = appState.mode != lastRenderedMode;
  bool promptChanged = appState.hb.prompt.id != lastRenderedPromptId;
  if (!force && !modeChanged && !promptChanged) {
    // In Idle, we still re-render on every heartbeat for fresh numbers.
    // That's handled by the caller passing force=true on heartbeat.
    return;
  }
  switch (appState.mode) {
    case Mode::BleInit:      renderBoot("BLE init..."); break;
    case Mode::Advertising:  {
      std::string n = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
      renderAdvertising(n.c_str()); break;
    }
    case Mode::Connected:    renderConnected(); break;
    case Mode::Idle:         renderIdle(appState); break;
    case Mode::Prompt:       renderPrompt(appState); break;
    case Mode::Ack:          renderAck(appState); break;
    case Mode::Disconnected: renderDisconnected(); break;
    case Mode::Fatal:        renderFatal("see serial"); break;
  }
  lastRenderedMode = appState.mode;
  lastRenderedPromptId = appState.hb.prompt.id;
}

static void onLine(const std::string& line) {
  uint32_t now = millis();
  ParsedMessage m = parseLine(line);
  bool changed = false;
  switch (m.kind) {
    case MessageKind::Heartbeat:
      // Connected→Idle/Prompt transition uses first heartbeat
      if (appState.mode == Mode::Connected ||
          appState.mode == Mode::Disconnected) {
        applyConnected(appState);
      }
      changed = applyHeartbeat(appState, m.heartbeat, now);
      render(true);  // always redraw on heartbeat for fresh numbers
      break;
    case MessageKind::Owner:
      changed = applyOwner(appState, m.ownerName);
      if (changed) render(true);
      break;
    case MessageKind::Time:
      // MVP: ignore (no clock displayed)
      break;
    case MessageKind::Unknown:
    case MessageKind::ParseError:
      // ignore
      break;
  }
}

void setup() {
  Serial.begin(115200);
  initUi();
  initButtons();
  renderBoot("BLE init...");

  appState.mode = Mode::BleInit;
  if (!initBle(deviceSuffix(), onLine)) {
    appState.mode = Mode::Fatal;
    render(true);
    while (1) delay(1000);
  }
  appState.mode = Mode::Advertising;
  render(true);
}

void loop() {
  uint32_t now = millis();
  pollBle();

  // Connection state → mode sync
  bool conn = isBleConnected();
  if (conn && appState.mode == Mode::Advertising) {
    applyConnected(appState);
    render(true);
  } else if (!conn && (appState.mode == Mode::Idle ||
                        appState.mode == Mode::Prompt ||
                        appState.mode == Mode::Ack ||
                        appState.mode == Mode::Connected)) {
    applyDisconnect(appState);
    render(true);
  } else if (!conn && appState.mode == Mode::Disconnected &&
             lastRenderedMode != Mode::Advertising) {
    // rpcBLE auto-re-advertises on disconnect; reflect it
    appState.mode = Mode::Advertising;
    render(true);
  }

  // Buttons (with post-send lockout)
  if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS) {
    ButtonEvent e = pollButtons(now);
    if (e == ButtonEvent::PressNav) {
      markInteraction(now);
    } else if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
      markInteraction(now);
      char btn = (e == ButtonEvent::PressA) ? 'A' : 'C';
      PermissionDecision d;
      std::string id;
      if (applyButton(appState, btn, now, d, id)) {
        std::string line = formatPermission(id, d);
        sendLine(line);
        lastButtonSendMs = now;
        render(true);
      }
    }
  }

  // Timeouts
  if (applyTimeouts(appState, now)) render(true);

  // Backlight dim when idle for too long
  if (appState.mode == Mode::Idle &&
      lastInteractionMs != 0 &&
      (now - lastInteractionMs) > BACKLIGHT_IDLE_MS) {
    setBacklight(20);
  }

  delay(10);
}
```

- [ ] **Step 8.2: Build**

Run: `pio run -e seeed_wio_terminal`
Expected: compile clean. Resolve any errors before uploading.

- [ ] **Step 8.3: Run native tests to make sure state/protocol logic isn't broken**

Run: `pio test -e native`
Expected: **18 tests PASS**.

- [ ] **Step 8.4: Flash and end-to-end test with Claude Desktop**

Preconditions:
- Claude Desktop has **Developer Mode enabled** (Help → Troubleshooting)
- RTL8720DN firmware is up to date (README step)

Steps:
1. Upload: `pio run -e seeed_wio_terminal -t upload`
2. Monitor: `pio device monitor -b 115200`
3. Screen shows **Advertising** with the full device name
4. On Claude Desktop: Developer menu → Open Hardware Buddy → Connect → pick `Claude-Wio-XXXX`
5. Screen changes to **Connected**, then **Idle** after the first heartbeat
6. Verify numbers and `msg` update as you run a Claude Code session
7. Trigger a permission prompt in Claude Code (e.g. run a Bash command). Wio screen flips to **Prompt**
8. Press **A** (right top button): screen shows **Approved** for 1s, then Idle — and Claude proceeds
9. Repeat but press **C**: shows **Denied**, Claude rejects the tool
10. Unplug Wio Terminal, wait 35s: when re-plugged/advertised it reconnects

If any step fails, debug via serial logs and narrow to the specific module. Do not mark task complete until all 10 steps pass.

- [ ] **Step 8.5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire MVP A end-to-end (BLE + state + UI + buttons)"
```

---

## Task 9: README

**Files:**
- Create: `README.md`

- [ ] **Step 9.1: Write `README.md`**

````markdown
# Wio Terminal Claude Buddy (MVP A)

A Wio Terminal firmware that connects to Claude Desktop over BLE, shows
heartbeat status, and lets you approve/deny permission prompts with the
top buttons.

Port of the BLE protocol from Anthropic's reference
[`claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy)
(written for M5StickC Plus / ESP32). This is an independent implementation
— only the wire protocol is shared.

## What works (MVP A)

- BLE Nordic UART Service, pairs with Claude Desktop's Hardware Buddy
- Idle screen: total / running / waiting counts + latest `msg`
- Permission prompt screen: tool name, hint, approve (A) / deny (C)
- Auto-reconnect on disconnect

## Not (yet) included

No pets, no GIF character packs, no transcript scrolling, no stats
persistence. See `docs/superpowers/specs/` for the phased plan.

## Hardware

- **Wio Terminal** (Seeed Studio, ATSAMD51 + RTL8720DN)
- USB-C cable

## Prerequisites

### 1. Install PlatformIO

See <https://docs.platformio.org/en/latest/core/installation/>.

### 2. Update the RTL8720DN firmware (mandatory)

The Wio Terminal's BLE runs on a separate Realtek RTL8720DN co-processor.
Out-of-box firmware does not support the BLE API used here. Update it
using Seeed's `ambd_flash_tool`.

Refer to **Seeed Studio's Wio Terminal wiki**, specifically the
"Network Overview" / "Update the wireless core firmware" pages, and run
`ambd_flash_tool` to flash the matching `ambd_firmware_*.bin` that ships
with the `Seeed_Arduino_rpcUnified` library (the one pulled in by
`platformio.ini`).

Without this step **BLE will not advertise**. The firmware flash is a
one-time thing per Wio Terminal unit.

### 3. Enable Developer Mode in Claude Desktop

`Help → Troubleshooting → Enable Developer Mode`. Then
`Developer → Open Hardware Buddy…`.

## Flashing

```bash
pio run -e seeed_wio_terminal -t upload
pio device monitor -b 115200
```

To enter bootloader mode if a regular upload fails, **double-tap the
power switch** (left side, slide down) — the green LED should pulse.

## Running tests

Pure-logic modules (`protocol`, `state`) are unit-tested on the host via
PlatformIO's `native` env:

```bash
pio test -e native
```

## Pairing

1. Power on the Wio Terminal — it shows `advertising as Claude-Wio-XXXX`
2. In Claude Desktop: Developer menu → Open Hardware Buddy → **Connect**
3. Pick `Claude-Wio-XXXX` from the list
4. The screen should change to `connected, waiting...` then the Idle screen
   once the first heartbeat arrives.

## Buttons

| Button | Idle | Prompt |
|---|---|---|
| KEY A (right top) | — | Approve |
| KEY C (left top)  | — | Deny |
| 5-way press       | wake backlight | wake backlight |

## License

MIT (see LICENSE).
````

- [ ] **Step 9.2: Commit**

```bash
git add README.md
git commit -m "docs: add README with setup, firmware update, and usage"
```

---

## Task 10: Final Verification & Cleanup

**Files:** (no new files, verification only)

- [ ] **Step 10.1: Run the full test suite**

Run: `pio test -e native`
Expected: **18/18 PASS**.

- [ ] **Step 10.2: Verify firmware still builds cleanly**

Run: `pio run -e seeed_wio_terminal`
Expected: build succeeds with no warnings-as-errors. Note binary size (should fit comfortably in 512KB flash).

- [ ] **Step 10.3: Re-run end-to-end with Claude Desktop**

Full handshake: fresh boot → Advertising → Connect from Desktop → Idle → trigger a permission prompt → Approve via A → verify Claude Code proceeds.

Do this on a **clean session** (power-cycle the Wio Terminal) to catch any state-initialization bugs that don't show on re-flash.

- [ ] **Step 10.4: Verify git log is clean**

Run: `git log --oneline`
Expected: 9 commits (one per task 1–9 plus the pre-existing spec commit). Each commit message should describe one logical change.

- [ ] **Step 10.5: Tag the MVP A release**

```bash
git tag -a mvp-a -m "Wio Terminal Claude Buddy MVP A: BLE + heartbeat + approve/deny"
```

No push — user controls remote.

---

## Self-Review Summary

- **Spec coverage:** Every section of the spec is implemented —
  architecture (tasks 1, 7), protocol scope (task 4), UI layout (task 5),
  button mapping (task 6), state machine (task 3), project structure (all),
  testing strategy (tasks 3 + 4 native tests), risks (README firmware step).
- **Placeholders:** None. Every code block contains real, compilable content.
- **Types consistent:** `AppState`, `HeartbeatData`, `PromptData`,
  `PermissionDecision`, `ButtonEvent` defined once and referenced
  consistently. Function signatures match across header and usage.
- **Hardware-only modules** (`ui`, `buttons`, `ble_nus`) have smoke tests
  instead of unit tests — intentional, documented in plan header.
