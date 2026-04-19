# Idle Auto-Off Backlight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn LCD backlight off after 60 seconds of button-inactivity; wake on any button press or new prompt arrival. Prompt mode never sleeps.

**Architecture:** New `src/backlight.{h,cpp}` module encapsulates awake/asleep state. GPIO writes happen only on state transitions (edge-triggered). `#ifdef ARDUINO` guards let `digitalWrite` stay out of native test builds while tests observe state via test-only accessors.

**Tech Stack:** Arduino framework, PlatformIO, Unity test framework on `[env:native]`.

**Spec:** `docs/superpowers/specs/2026-04-19-idle-auto-off-design.md`

---

## Task 0: Branch setup

**Files:** No code changes

- [ ] **Step 1: Create worktree + feature branch**

From repo root `/Users/tzangms/projects/wioclaude`:

```bash
git worktree add .worktrees/idle-auto-off -b feature/idle-auto-off
cd .worktrees/idle-auto-off
```

- [ ] **Step 2: Verify baseline builds and tests pass**

```bash
pio run -e seeed_wio_terminal
pio test -e native
```

Expected: both succeed; `pio test -e native` shows 35/35.

---

## Task 1: Config constant + native build filter

**Files:**
- Modify: `src/config.h`
- Modify: `platformio.ini`

- [ ] **Step 1: Add `BACKLIGHT_IDLE_MS` to `src/config.h`**

Find the existing `Timeouts (ms)` block and add a new line alongside:

```cpp
// --- Timeouts (ms) ---
static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 30000;
static constexpr uint32_t ACK_DISPLAY_MS       = 1000;
static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 20;
static constexpr uint32_t POST_SEND_LOCKOUT_MS = 500;
static constexpr uint32_t BACKLIGHT_IDLE_MS    = 60000;
```

- [ ] **Step 2: Update `[env:native]` build filter in `platformio.ini`**

Change:

```ini
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp>
```

to:

```ini
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp>
```

- [ ] **Step 3: Verify device build still compiles**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS (native env not built yet — `backlight.cpp` will be created in Task 2).

- [ ] **Step 4: Commit**

```bash
git add src/config.h platformio.ini
git commit -m "feat(backlight): add BACKLIGHT_IDLE_MS constant and native build filter"
```

---

## Task 2: Backlight module skeleton with init + isAwake (TDD)

**Files:**
- Create: `src/backlight.h`
- Create: `src/backlight.cpp`
- Create: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Create `test/test_backlight/test_backlight.cpp` with a failing init test**

```cpp
#include <unity.h>
#include "backlight.h"
#include "state.h"
#include "config.h"

void test_init_starts_awake() {
  backlightInit();
  TEST_ASSERT_TRUE(backlightIsAwake());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_starts_awake);
  return UNITY_END();
}
```

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_backlight
```

Expected: build fails because `backlight.h` does not exist yet.

- [ ] **Step 3: Create `src/backlight.h`**

```cpp
#pragma once

#include <cstdint>

struct AppState;

void backlightInit();
void backlightWake(uint32_t nowMs);
void backlightTick(const AppState& s, uint32_t nowMs);
bool backlightIsAwake();

#ifndef ARDUINO
// Test-only accessors (native build only).
int _backlightWriteCount();
bool _backlightLastWritten();
#endif
```

- [ ] **Step 4: Create `src/backlight.cpp` with init + isAwake (other functions stubbed to no-op)**

```cpp
#include "backlight.h"
#include "config.h"
#include "state.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace {
  bool awake = true;
  uint32_t lastActivityMs = 0;

#ifdef ARDUINO
  void writePin(bool high) {
    digitalWrite(LCD_BACKLIGHT, high ? HIGH : LOW);
  }
#else
  int writeCount = 0;
  bool lastWritten = true;
  void writePin(bool high) { lastWritten = high; ++writeCount; }
#endif
}

#ifndef ARDUINO
int _backlightWriteCount() { return writeCount; }
bool _backlightLastWritten() { return lastWritten; }
#endif

void backlightInit() {
#ifdef ARDUINO
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
#else
  writeCount = 0;
  lastWritten = true;
#endif
  awake = true;
  lastActivityMs = 0;
}

void backlightWake(uint32_t /*nowMs*/) {
  // Implemented in Task 4
}

void backlightTick(const AppState& /*s*/, uint32_t /*nowMs*/) {
  // Implemented in Task 3
}

bool backlightIsAwake() {
  return awake;
}
```

- [ ] **Step 5: Run — expect pass**

```bash
pio test -e native -f test_backlight
```
Expected: 1 test passes.

- [ ] **Step 6: Verify full native suite still green**

```bash
pio test -e native
```
Expected: 36/36 (35 previous + 1 new).

- [ ] **Step 7: Verify device build compiles**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/backlight.h src/backlight.cpp test/test_backlight/test_backlight.cpp
git commit -m "feat(backlight): module skeleton with init and isAwake"
```

---

## Task 3: Implement backlightTick sleep path (TDD)

**Files:**
- Modify: `src/backlight.cpp`
- Modify: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Add failing test**

Append to `test/test_backlight/test_backlight.cpp` before `int main`:

```cpp
void test_tick_without_activity_sleeps_after_timeout() {
  backlightInit();
  AppState s;
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS - 1);
  TEST_ASSERT_TRUE(backlightIsAwake());
  backlightTick(s, BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
}
```

Add `RUN_TEST(test_tick_without_activity_sleeps_after_timeout);` to `main()`.

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_backlight
```

Expected: `test_tick_without_activity_sleeps_after_timeout` fails (backlightTick is a stub).

- [ ] **Step 3: Implement `backlightTick` sleep path in `src/backlight.cpp`**

Replace the stub:

```cpp
void backlightTick(const AppState& s, uint32_t nowMs) {
  if (!awake) return;
  if (s.mode == Mode::Prompt) {
    lastActivityMs = nowMs;
    return;
  }
  if ((nowMs - lastActivityMs) < BACKLIGHT_IDLE_MS) return;
  writePin(false);
  awake = false;
}
```

- [ ] **Step 4: Run — expect pass**

```bash
pio test -e native -f test_backlight
```

Expected: 2 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/backlight.cpp test/test_backlight/test_backlight.cpp
git commit -m "feat(backlight): tick transitions awake → asleep after timeout"
```

---

## Task 4: Implement backlightWake (TDD)

**Files:**
- Modify: `src/backlight.cpp`
- Modify: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Add failing test**

Append to `test/test_backlight/test_backlight.cpp`:

```cpp
void test_wake_from_sleep_restores_awake() {
  backlightInit();
  AppState s;
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
  backlightWake(100000);
  TEST_ASSERT_TRUE(backlightIsAwake());
}
```

Add `RUN_TEST(test_wake_from_sleep_restores_awake);` to `main()`.

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_backlight
```

Expected: `test_wake_from_sleep_restores_awake` fails (backlightWake is a stub).

- [ ] **Step 3: Implement `backlightWake`**

Replace the stub in `src/backlight.cpp`:

```cpp
void backlightWake(uint32_t nowMs) {
  lastActivityMs = nowMs;
  if (!awake) {
    writePin(true);
    awake = true;
  }
}
```

- [ ] **Step 4: Run — expect pass**

```bash
pio test -e native -f test_backlight
```

Expected: 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/backlight.cpp test/test_backlight/test_backlight.cpp
git commit -m "feat(backlight): wake restores awake and resets timer"
```

---

## Task 5: Prompt mode protection (TDD)

**Files:**
- Modify: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Add prompt-mode tests**

Append to `test/test_backlight/test_backlight.cpp`:

```cpp
void test_prompt_mode_never_sleeps() {
  backlightInit();
  AppState s;
  s.mode = Mode::Prompt;
  backlightTick(s, BACKLIGHT_IDLE_MS * 10);
  TEST_ASSERT_TRUE(backlightIsAwake());
}

void test_prompt_mode_resets_timer_on_exit() {
  backlightInit();
  AppState s;
  s.mode = Mode::Prompt;
  backlightTick(s, BACKLIGHT_IDLE_MS * 2);     // still in Prompt, 2 min elapsed
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS * 2 + 1); // just exited, should NOT sleep immediately
  TEST_ASSERT_TRUE(backlightIsAwake());
  // But after another full timeout, should sleep
  backlightTick(s, BACKLIGHT_IDLE_MS * 2 + BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
}
```

Add both `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run — expect pass**

```bash
pio test -e native -f test_backlight
```

Expected: 5 tests pass. The logic in Task 3 already handles Prompt mode; these tests simply lock in the behavior.

If either test fails, re-read the `backlightTick` implementation — the Prompt branch must both `lastActivityMs = nowMs` AND `return`.

- [ ] **Step 3: Commit**

```bash
git add test/test_backlight/test_backlight.cpp
git commit -m "test(backlight): Prompt mode never sleeps and resets timer on exit"
```

---

## Task 6: Edge-only writes verification (TDD)

**Files:**
- Modify: `test/test_backlight/test_backlight.cpp`

- [ ] **Step 1: Add edge-write tests**

Append to `test/test_backlight/test_backlight.cpp`:

```cpp
void test_sleep_writes_pin_exactly_once() {
  backlightInit();  // resets writeCount to 0
  AppState s;
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS + 1);  // awake → asleep
  int afterSleep = _backlightWriteCount();
  backlightTick(s, BACKLIGHT_IDLE_MS + 100);  // still asleep
  backlightTick(s, BACKLIGHT_IDLE_MS + 200);
  TEST_ASSERT_EQUAL(afterSleep, _backlightWriteCount());  // no additional writes
  TEST_ASSERT_FALSE(_backlightLastWritten());
}

void test_wake_is_idempotent_when_already_awake() {
  backlightInit();
  int initial = _backlightWriteCount();
  backlightWake(1000);
  TEST_ASSERT_EQUAL(initial, _backlightWriteCount());  // no write since already awake
  backlightWake(2000);
  TEST_ASSERT_EQUAL(initial, _backlightWriteCount());
}
```

Add both `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run — expect pass**

```bash
pio test -e native -f test_backlight
```

Expected: 7 tests pass. Implementation from Tasks 3–4 already has the correct edge gating (`if (!awake)` in both paths).

- [ ] **Step 3: Run full suite one more time**

```bash
pio test -e native
```

Expected: 42/42 (35 previous + 7 new).

- [ ] **Step 4: Commit**

```bash
git add test/test_backlight/test_backlight.cpp
git commit -m "test(backlight): verify edge-only pin writes"
```

---

## Task 7: Remove dead BACKLIGHT_FULL / BACKLIGHT_DIM + clean initUi

**Files:**
- Modify: `src/config.h`
- Modify: `src/ui.cpp`

- [ ] **Step 1: Remove constants from `src/config.h`**

Delete these two lines (and the `// --- Backlight ---` section header they live under):

```cpp
// --- Backlight ---
static constexpr uint8_t BACKLIGHT_FULL = 255;
static constexpr uint8_t BACKLIGHT_DIM  = 50;
```

- [ ] **Step 2: Remove backlight GPIO setup from `src/ui.cpp` `initUi`**

Find `initUi` which currently looks like:

```cpp
void initUi() {
  pinMode(LCD_BACKLIGHT, OUTPUT);
  analogWrite(LCD_BACKLIGHT, BACKLIGHT_FULL);
  tft.begin();
  tft.setRotation(3);
  clearAll();
}
```

Replace with:

```cpp
void initUi() {
  tft.begin();
  tft.setRotation(3);
  clearAll();
}
```

The backlight setup moves to `backlightInit()` (called separately from `main.cpp`).

- [ ] **Step 3: Verify device build**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. If fails with "BACKLIGHT_FULL not declared", some other file references it — grep the codebase. Only `ui.cpp` uses it in MVP A / SP1, so this should be clean.

- [ ] **Step 4: Verify native tests still green**

```bash
pio test -e native
```
Expected: 42/42.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/ui.cpp
git commit -m "refactor(backlight): remove dead BACKLIGHT_FULL/DIM constants

initUi no longer touches LCD_BACKLIGHT GPIO; that moves to backlightInit
(called explicitly from setup() in the next task)."
```

---

## Task 8: Integrate in main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add `#include "backlight.h"`**

At the top of `src/main.cpp`, in the project-includes block, add:

```cpp
#include "backlight.h"
```

- [ ] **Step 2: Call `backlightInit()` in `setup()`**

Find `setup()` currently looking like:

```cpp
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  initButtons();
  renderBoot("BLE init...");

  appState.deviceName = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
  appState.mode = Mode::BleInit;
  ...
```

Add `backlightInit();` after `initButtons();`:

```cpp
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  initButtons();
  backlightInit();
  renderBoot("BLE init...");
  ...
```

- [ ] **Step 3: Wake on button press in `loop()`**

Find the button handling block:

```cpp
if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS) {
  ButtonEvent e = pollButtons(now);
  if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
    ...
  }
}
```

Change to:

```cpp
if ((now - lastButtonSendMs) > POST_SEND_LOCKOUT_MS) {
  ButtonEvent e = pollButtons(now);
  if (e != ButtonEvent::None) {
    backlightWake(now);
  }
  if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
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
```

- [ ] **Step 4: Add `backlightTick` after `applyTimeouts`**

Find:

```cpp
if (applyTimeouts(appState, now)) render(true);

delay(10);
```

Change to:

```cpp
if (applyTimeouts(appState, now)) render(true);

backlightTick(appState, now);

delay(10);
```

- [ ] **Step 5: Wake on new prompt in `onLine()`**

Find the Heartbeat case:

```cpp
case MessageKind::Heartbeat:
  if (appState.mode == Mode::Connected ||
      appState.mode == Mode::Disconnected) {
    applyConnected(appState);
  }
  applyHeartbeat(appState, std::move(m.heartbeat), now);
  pendingRender = true;
  break;
```

Change to:

```cpp
case MessageKind::Heartbeat: {
  if (appState.mode == Mode::Connected ||
      appState.mode == Mode::Disconnected) {
    applyConnected(appState);
  }
  bool prevHasPrompt = appState.hb.hasPrompt;
  std::string prevPromptId = appState.hb.prompt.id;
  applyHeartbeat(appState, std::move(m.heartbeat), now);
  if ((!prevHasPrompt && appState.hb.hasPrompt) ||
      (appState.hb.hasPrompt && appState.hb.prompt.id != prevPromptId)) {
    backlightWake(now);
  }
  pendingRender = true;
  break;
}
```

Note: added `{}` around the case body because of new local variables.

- [ ] **Step 6: Verify device build**

```bash
pio run -e seeed_wio_terminal
```

Expected: SUCCESS. Memory usage should be within a few bytes of previous build.

- [ ] **Step 7: Verify native tests**

```bash
pio test -e native
```

Expected: 42/42.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat(backlight): wire init/wake/tick into main

- backlightInit in setup
- any button press calls backlightWake
- backlightTick each loop (handles timeout + Prompt mode exception)
- new prompt arrival (hasPrompt false→true OR prompt.id change) wakes screen"
```

---

## Task 9: Device smoke test + PR

**Files:** No code changes (manual verification)

- [ ] **Step 1: Upload**

```bash
pio run -e seeed_wio_terminal -t upload
```

If upload fails with "Couldn't find a board": double-click Wio power button to enter bootloader, retry.

- [ ] **Step 2: Manual smoke test checklist**

Connect Claude Desktop's Hardware Buddy, then:

- [ ] Leave device idle for **70 seconds** → LCD backlight turns off
- [ ] Press any button (A / B / C / 5-way) → screen lights up, content intact
- [ ] Leave idle again → off again after 60s
- [ ] Ask Claude to run a command needing permission (e.g. `ls ~`) → screen wakes when prompt arrives
- [ ] Leave the prompt unhandled for 2 minutes → screen stays on (Prompt mode protection)
- [ ] Press A to approve → "Approved" shown, returns to Idle, 60s later → off again

If any checkpoint fails, inspect serial log and `git diff` on backlight module.

- [ ] **Step 3: Push branch**

```bash
git push -u origin feature/idle-auto-off
```

- [ ] **Step 4: Open PR**

```bash
gh pr create --title "Idle auto-off backlight" --body "$(cat <<'EOF'
## Summary
Implements 60-second idle backlight-off with robust wake. Replaces the removed buggy dim logic from SP1 (which tried `analogWrite` on a NOT_ON_PWM pin).

Spec: `docs/superpowers/specs/2026-04-19-idle-auto-off-design.md`
Plan: `docs/superpowers/plans/2026-04-19-idle-auto-off.md`

**Behavior:**
- 60s without button press or new prompt → backlight off
- Any button press → wake
- New prompt (hasPrompt false→true or prompt.id change) → wake
- Prompt mode never sleeps; Prompt→Idle transition resets 60s timer
- Screen content preserved (no redraw on wake)

**Implementation:**
- New `src/backlight.{h,cpp}` module with edge-triggered GPIO
- `#ifdef ARDUINO` guards let native tests observe writes via `_backlightWriteCount()` / `_backlightLastWritten()`
- 7 native unit tests (init, sleep, wake, prompt protection, prompt-exit reset, edge-writes, idempotent wake)

## Test plan
- [x] `pio test -e native`: 42/42
- [x] `pio run -e seeed_wio_terminal`: builds clean
- [x] Device smoke test (see plan Task 9 Step 2) — all 6 checkpoints pass

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Self-Review

### Spec coverage
- ✅ 60s timeout → Task 1 (constant), Task 3 (tick)
- ✅ Button wake → Task 8 Step 3
- ✅ New prompt wake → Task 8 Step 5
- ✅ Prompt never sleeps → Task 3 (implementation), Task 5 (test lock-in)
- ✅ Prompt→Idle timer reset → Task 3 (implementation via `lastActivityMs = nowMs` in Prompt branch), Task 5 (test lock-in)
- ✅ Content preserved → no redraw is called on wake (verified by absence of `render` in Task 8 Step 3)
- ✅ Edge-only GPIO writes → Task 6 tests verify
- ✅ Native testability → Task 2 skeleton sets up `#ifdef ARDUINO` infrastructure
- ✅ MVP A / SP1 regression → Task 9 smoke test + full `pio test -e native`
- ✅ Remove dead BACKLIGHT_FULL / BACKLIGHT_DIM → Task 7
- ✅ `initUi` no longer touches backlight → Task 7 Step 2
- ✅ `#ifdef ARDUINO` helper pattern → Task 2 Step 4

### Placeholder scan
No TBD / TODO / "add appropriate X" / "similar to Task N". Every code block shows complete code.

### Type consistency
- `backlightInit()`, `backlightWake(uint32_t nowMs)`, `backlightTick(const AppState&, uint32_t nowMs)`, `backlightIsAwake() → bool` — consistent across all tasks
- `BACKLIGHT_IDLE_MS` used identically in tests (Task 3, 4, 5, 6) and implementation (Task 3)
- `_backlightWriteCount()`, `_backlightLastWritten()` declared in Task 2, used in Task 6

No issues found.
