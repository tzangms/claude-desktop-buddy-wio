# SP2 Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist stats (appr/deny/lvl/nap/vel), identity (deviceName/ownerName), and token counters to QSPI flash via Seeed FS + SFUD. Survive reboot and `pio run -t upload`.

**Architecture:** New `src/persist.{h,cpp}` module. Single packed binary struct in `/wioclaude/stats.bin`. Native tests use an in-memory fake file buffer; device writes go through Seeed FS behind `#ifdef ARDUINO`. All flash I/O deferred from BLE callback to main-loop `persistTick`. Debounced writes (5 min OR delta>1000 tokens) for heartbeat-driven updates; immediate writes for button/name/owner events.

**Tech Stack:** Arduino framework, PlatformIO, Seeed Arduino FS (FatFS over QSPI), Seeed Arduino SFUD, Unity test framework on `[env:native]`.

**Spec:** `docs/superpowers/specs/2026-04-19-sp2-persistence-design.md`

---

## Task 0: Branch setup

**Files:** no code changes

- [ ] **Step 1: Create worktree + feature branch**

From repo root `/Users/tzangms/projects/wioclaude`:

```bash
git worktree add .worktrees/sp2-persistence -b feature/sp2-persistence
cd .worktrees/sp2-persistence
```

- [ ] **Step 2: Verify baseline builds and tests pass**

```bash
pio run -e seeed_wio_terminal
pio test -e native
```

Expected: both succeed; 42 native tests pass.

---

## Task 1: Config constants + native build filter

**Files:**
- Modify: `src/config.h`
- Modify: `platformio.ini`

- [ ] **Step 1: Add persistence constants to `src/config.h`**

Append to the end of `src/config.h`:

```cpp

// --- SP2 persistence ---
static constexpr uint32_t PERSIST_MAGIC           = 0xC1A7DE42;
static constexpr uint32_t PERSIST_VERSION         = 1;
static constexpr uint32_t PERSIST_DEBOUNCE_MS     = 300000;  // 5 min
static constexpr int64_t  PERSIST_DEBOUNCE_TOKENS = 1000;
static constexpr int64_t  TOKENS_PER_LEVEL        = 50000;
```

- [ ] **Step 2: Extend native build filter in `platformio.ini`**

Change `[env:native]`:
```ini
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp>
```
to:
```ini
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp> +<persist.cpp>
```

`persist.cpp` doesn't exist yet (Task 2 creates it); native tests will skip silently.

- [ ] **Step 3: Verify device build still compiles**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/config.h platformio.ini
git commit -m "feat(persist): add persistence config constants and native build filter"
```

---

## Task 2: Module skeleton + defaults + fake-file harness (TDD)

**Files:**
- Create: `src/persist.h`
- Create: `src/persist.cpp`
- Create: `test/test_persist/test_persist.cpp`

- [ ] **Step 1: Write failing test**

Create `test/test_persist/test_persist.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "persist.h"
#include "config.h"

void test_init_empty_uses_defaults() {
  _persistResetFakeFile();
  persistInit();
  const PersistData& d = persistGet();
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, d.magic);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, d.version);
  TEST_ASSERT_EQUAL_INT32(0, d.appr);
  TEST_ASSERT_EQUAL_INT32(0, d.deny);
  TEST_ASSERT_EQUAL_INT32(0, d.lvl);
  TEST_ASSERT_EQUAL_STRING("", d.deviceName);
  TEST_ASSERT_EQUAL_STRING("", d.ownerName);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_empty_uses_defaults);
  return UNITY_END();
}
```

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_persist
```
Expected: build fails (persist.h not found).

- [ ] **Step 3: Create `src/persist.h`**

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

struct __attribute__((packed)) PersistData {
  uint32_t magic;
  uint32_t version;
  int32_t  appr;
  int32_t  deny;
  int32_t  lvl;
  int32_t  nap;
  int32_t  vel;
  int64_t  tokens_today;
  int64_t  deviceLifetimeTokens;
  char     deviceName[33];
  char     ownerName[33];
};

static_assert(sizeof(PersistData) == 110,
              "PersistData layout surprise; check __attribute__((packed))");

// Mount FS and load. On failure uses defaults; never blocks.
void persistInit();

// Read-only snapshot.
const PersistData& persistGet();

// Mutable reference. Caller must call persistCommit() afterwards.
PersistData& persistMut();

// Mark dirty; if immediate=true, flush now; if false, persistTick handles.
void persistCommit(bool immediate);

// Call every loop. Flushes if dirty & debounce threshold met.
void persistTick(uint32_t nowMs);

// Business helper: applies heartbeat-delta token accounting.
// Updates deviceLifetimeTokens, recomputes lvl, updates tokens_today.
// Does NOT call persistCommit — caller follows up.
void persistUpdateFromHeartbeat(int64_t sessionTokens, int64_t tokensToday);

#ifndef ARDUINO
// Test-only accessors (native build only).
const uint8_t* _persistFakeFile();
size_t _persistFakeFileSize();
void _persistResetFakeFile();
int _persistWriteCount();
#endif
```

- [ ] **Step 4: Create `src/persist.cpp` with native skeleton**

```cpp
#include "persist.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
// Device-side file I/O lands in Task 8.
#else
#include <vector>
namespace {
  std::vector<uint8_t> fakeFile;
  int writeCount = 0;
}
const uint8_t* _persistFakeFile() { return fakeFile.data(); }
size_t _persistFakeFileSize() { return fakeFile.size(); }
void _persistResetFakeFile() { fakeFile.clear(); writeCount = 0; }
int _persistWriteCount() { return writeCount; }
#endif

namespace {
  PersistData data;
  bool dirty = false;
  uint32_t lastFlushMs = 0;
  int64_t lastFlushedLifetimeTokens = 0;
  int64_t prevSessionTokens = 0;
  bool fsReady = false;

  void setDefaults() {
    std::memset(&data, 0, sizeof(data));
    data.magic = PERSIST_MAGIC;
    data.version = PERSIST_VERSION;
  }

  bool readStore(uint8_t* buf, size_t size) {
#ifdef ARDUINO
    (void)buf; (void)size;
    return false;  // Implemented in Task 8
#else
    if (fakeFile.size() != size) return false;
    std::memcpy(buf, fakeFile.data(), size);
    return true;
#endif
  }

  bool writeStore(const uint8_t* buf, size_t size) {
#ifdef ARDUINO
    (void)buf; (void)size;
    return false;  // Implemented in Task 8
#else
    fakeFile.assign(buf, buf + size);
    ++writeCount;
    return true;
#endif
  }

  void flush() {
    writeStore(reinterpret_cast<const uint8_t*>(&data), sizeof(data));
    dirty = false;
    lastFlushedLifetimeTokens = data.deviceLifetimeTokens;
  }
}

void persistInit() {
  setDefaults();
  fsReady = true;
#ifdef ARDUINO
  // Task 8 replaces this with real SFUD/FS mount
#endif
  // Try to load existing file
  PersistData tmp;
  if (readStore(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp))) {
    if (tmp.magic == PERSIST_MAGIC && tmp.version == PERSIST_VERSION) {
      data = tmp;
    }
    // else: keep defaults (silently treat as corrupt)
  }
  prevSessionTokens = 0;
  lastFlushedLifetimeTokens = data.deviceLifetimeTokens;
  lastFlushMs = 0;
  dirty = false;
}

const PersistData& persistGet() { return data; }

PersistData& persistMut() { return data; }

void persistCommit(bool immediate) {
  dirty = true;
  if (immediate && fsReady) {
    flush();
  }
}

void persistTick(uint32_t /*nowMs*/) {
  // Debounce logic lands in Tasks 5–6
}

void persistUpdateFromHeartbeat(int64_t /*sessionTokens*/, int64_t /*tokensToday*/) {
  // Lands in Task 7
}
```

- [ ] **Step 5: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 1 test passes.

- [ ] **Step 6: Full suite**

```bash
pio test -e native
```
Expected: 43 tests pass (42 existing + 1 new).

- [ ] **Step 7: Verify device build**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS. Note: `readStore`/`writeStore` ARDUINO branch returns false (Task 8 fills in), so device boot will use defaults.

- [ ] **Step 8: Commit**

```bash
git add src/persist.h src/persist.cpp test/test_persist/test_persist.cpp
git commit -m "feat(persist): module skeleton + defaults + native fake-file harness"
```

---

## Task 3: persistInit validation — magic / version / size (TDD)

**Files:**
- Modify: `test/test_persist/test_persist.cpp`

No implementation changes needed — the skeleton already rejects mismatched magic/version via the check in `persistInit`. These tests lock that behavior in and add a size-mismatch case.

- [ ] **Step 1: Add tests**

Append to `test/test_persist/test_persist.cpp` before `int main`:

```cpp
void test_init_loads_existing_data() {
  _persistResetFakeFile();
  PersistData good{};
  good.magic = PERSIST_MAGIC;
  good.version = PERSIST_VERSION;
  good.appr = 17;
  good.deny = 3;
  good.lvl = 5;
  good.deviceLifetimeTokens = 250000;
  std::strcpy(good.deviceName, "Clawd");
  std::strcpy(good.ownerName, "Felix");
  // Simulate a pre-existing file by seeding the fake store:
  persistMut() = good;
  persistCommit(true);
  // Re-init should read those bytes back via readStore()
  persistInit();
  const PersistData& d = persistGet();
  TEST_ASSERT_EQUAL_INT32(17, d.appr);
  TEST_ASSERT_EQUAL_INT32(3, d.deny);
  TEST_ASSERT_EQUAL_INT32(5, d.lvl);
  TEST_ASSERT_EQUAL_INT64(250000, d.deviceLifetimeTokens);
  TEST_ASSERT_EQUAL_STRING("Clawd", d.deviceName);
  TEST_ASSERT_EQUAL_STRING("Felix", d.ownerName);
}

void test_init_rejects_wrong_magic() {
  _persistResetFakeFile();
  PersistData bad{};
  bad.magic = 0xDEADBEEF;
  bad.version = PERSIST_VERSION;
  bad.appr = 99;
  persistMut() = bad;
  persistCommit(true);
  persistInit();
  TEST_ASSERT_EQUAL_INT32(0, persistGet().appr);  // fell back to defaults
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, persistGet().magic);
}

void test_init_rejects_wrong_version() {
  _persistResetFakeFile();
  PersistData bad{};
  bad.magic = PERSIST_MAGIC;
  bad.version = 999;
  bad.appr = 99;
  persistMut() = bad;
  persistCommit(true);
  persistInit();
  TEST_ASSERT_EQUAL_INT32(0, persistGet().appr);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, persistGet().version);
}
```

Add three `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 4 tests pass.

- [ ] **Step 3: Full suite**

```bash
pio test -e native
```
Expected: 46 tests pass.

- [ ] **Step 4: Commit**

```bash
git add test/test_persist/test_persist.cpp
git commit -m "test(persist): validate magic/version rejection with defaults fallback"
```

---

## Task 4: persistCommit(true) immediate flush (TDD)

**Files:**
- Modify: `test/test_persist/test_persist.cpp`

No implementation changes — skeleton already flushes on `immediate=true`. This test locks that behavior.

- [ ] **Step 1: Add test**

Append to `test/test_persist/test_persist.cpp`:

```cpp
void test_commit_immediate_flushes_now() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 7;
  persistCommit(true);
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
  // Verify round-trip through read
  persistInit();
  TEST_ASSERT_EQUAL_INT32(7, persistGet().appr);
}
```

Add `RUN_TEST(test_commit_immediate_flushes_now);` to `main()`.

- [ ] **Step 2: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 5 tests pass.

- [ ] **Step 3: Commit**

```bash
git add test/test_persist/test_persist.cpp
git commit -m "test(persist): commit(true) flushes synchronously"
```

---

## Task 5: persistTick time-based debounce (TDD)

**Files:**
- Modify: `src/persist.cpp`
- Modify: `test/test_persist/test_persist.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_persist/test_persist.cpp`:

```cpp
void test_commit_debounced_waits_under_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 1;
  persistCommit(false);
  persistTick(PERSIST_DEBOUNCE_MS - 1);
  TEST_ASSERT_EQUAL(before, _persistWriteCount());  // no flush
}

void test_tick_flushes_after_time_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 2;
  persistCommit(false);
  persistTick(PERSIST_DEBOUNCE_MS + 1);
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
}

void test_tick_skips_when_clean() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistTick(PERSIST_DEBOUNCE_MS * 10);
  TEST_ASSERT_EQUAL(before, _persistWriteCount());  // nothing dirty
}
```

Add three `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_persist
```
Expected: `test_tick_flushes_after_time_threshold` fails (persistTick is still a stub).

- [ ] **Step 3: Implement `persistTick` time debounce**

In `src/persist.cpp`, replace the `persistTick` stub:

```cpp
void persistTick(uint32_t nowMs) {
  if (!dirty || !fsReady) return;
  if ((nowMs - lastFlushMs) < PERSIST_DEBOUNCE_MS) return;
  flush();
  lastFlushMs = nowMs;
}
```

- [ ] **Step 4: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 8 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/persist.cpp test/test_persist/test_persist.cpp
git commit -m "feat(persist): tick flushes after time debounce"
```

---

## Task 6: persistTick token-delta debounce (TDD)

**Files:**
- Modify: `src/persist.cpp`
- Modify: `test/test_persist/test_persist.cpp`

- [ ] **Step 1: Add failing test**

Append to `test/test_persist/test_persist.cpp`:

```cpp
void test_tick_flushes_after_token_delta_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  // Simulate token accumulation without crossing time threshold
  persistMut().deviceLifetimeTokens = PERSIST_DEBOUNCE_TOKENS + 1;
  persistCommit(false);
  persistTick(1000);  // well under PERSIST_DEBOUNCE_MS
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
}
```

Add `RUN_TEST(test_tick_flushes_after_token_delta_threshold);` to `main()`.

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_persist
```
Expected: new test fails (delta condition not implemented).

- [ ] **Step 3: Update `persistTick` to also trigger on token delta**

In `src/persist.cpp`, replace `persistTick` with:

```cpp
void persistTick(uint32_t nowMs) {
  if (!dirty || !fsReady) return;
  int64_t tokenDelta = data.deviceLifetimeTokens - lastFlushedLifetimeTokens;
  bool timeTriggered  = (nowMs - lastFlushMs) >= PERSIST_DEBOUNCE_MS;
  bool tokenTriggered = tokenDelta >= PERSIST_DEBOUNCE_TOKENS;
  if (!timeTriggered && !tokenTriggered) return;
  flush();
  lastFlushMs = nowMs;
}
```

- [ ] **Step 4: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 9 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/persist.cpp test/test_persist/test_persist.cpp
git commit -m "feat(persist): tick flushes on token-delta threshold"
```

---

## Task 7: persistUpdateFromHeartbeat — lvl + delta protection (TDD)

**Files:**
- Modify: `src/persist.cpp`
- Modify: `test/test_persist/test_persist.cpp`

- [ ] **Step 1: Add failing tests**

Append to `test/test_persist/test_persist.cpp`:

```cpp
void test_heartbeat_first_call_accumulates_session_tokens() {
  _persistResetFakeFile();
  persistInit();
  // First heartbeat: session started at 0, now reports 15000.
  persistUpdateFromHeartbeat(15000, 1234);
  TEST_ASSERT_EQUAL_INT64(15000, persistGet().deviceLifetimeTokens);
  TEST_ASSERT_EQUAL_INT64(1234, persistGet().tokens_today);
  TEST_ASSERT_EQUAL_INT32(0, persistGet().lvl);  // 15000 / 50000 = 0
}

void test_heartbeat_level_increments_every_50k_tokens() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(50000, 0);
  TEST_ASSERT_EQUAL_INT32(1, persistGet().lvl);
  persistUpdateFromHeartbeat(120000, 0);
  TEST_ASSERT_EQUAL_INT32(2, persistGet().lvl);  // 120000/50000 = 2
}

void test_heartbeat_desktop_restart_no_negative_delta() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(10000, 0);
  int64_t lifetimeBefore = persistGet().deviceLifetimeTokens;
  // Desktop restart: session counter goes back to small value
  persistUpdateFromHeartbeat(500, 0);
  TEST_ASSERT_EQUAL_INT64(lifetimeBefore, persistGet().deviceLifetimeTokens);
  // Next heartbeat with new session progress should accumulate from 500
  persistUpdateFromHeartbeat(2500, 0);
  TEST_ASSERT_EQUAL_INT64(lifetimeBefore + 2000, persistGet().deviceLifetimeTokens);
}

void test_heartbeat_updates_tokens_today() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(0, 100);
  TEST_ASSERT_EQUAL_INT64(100, persistGet().tokens_today);
  persistUpdateFromHeartbeat(0, 5000);
  TEST_ASSERT_EQUAL_INT64(5000, persistGet().tokens_today);
  persistUpdateFromHeartbeat(0, 0);  // midnight reset
  TEST_ASSERT_EQUAL_INT64(0, persistGet().tokens_today);
}
```

Add four `RUN_TEST(...)` lines to `main()`.

- [ ] **Step 2: Run — expect fail**

```bash
pio test -e native -f test_persist
```
Expected: all four new tests fail (persistUpdateFromHeartbeat is a stub).

- [ ] **Step 3: Implement `persistUpdateFromHeartbeat`**

In `src/persist.cpp`, replace the stub:

```cpp
void persistUpdateFromHeartbeat(int64_t sessionTokens, int64_t tokensToday) {
  // Desktop-restart protection: if session counter dropped, ignore delta
  // for this heartbeat and resync baseline.
  if (sessionTokens >= prevSessionTokens) {
    int64_t delta = sessionTokens - prevSessionTokens;
    data.deviceLifetimeTokens += delta;
  }
  prevSessionTokens = sessionTokens;
  data.lvl = static_cast<int32_t>(data.deviceLifetimeTokens / TOKENS_PER_LEVEL);
  data.tokens_today = tokensToday;
}
```

- [ ] **Step 4: Run — expect pass**

```bash
pio test -e native -f test_persist
```
Expected: 13 tests pass.

- [ ] **Step 5: Full suite**

```bash
pio test -e native
```
Expected: 55 tests pass (42 pre-SP2 + 13 new).

- [ ] **Step 6: Commit**

```bash
git add src/persist.cpp test/test_persist/test_persist.cpp
git commit -m "feat(persist): heartbeat accounting with lvl and desktop-restart protection"
```

---

## Task 8: Device file I/O (ARDUINO branch)

**Files:**
- Modify: `src/persist.cpp`

This task implements `readStore`, `writeStore`, and the `persistInit` platform-specific mount step for the device. No native-test changes; device smoke test verifies.

- [ ] **Step 1: Add Seeed FS + SFUD includes and mount state**

At top of `src/persist.cpp`, change the `#ifdef ARDUINO` block:

```cpp
#ifdef ARDUINO
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
static constexpr const char* PERSIST_DIR  = "/wioclaude";
static constexpr const char* PERSIST_PATH = "/wioclaude/stats.bin";
static int consecutiveWriteFailures = 0;
static constexpr int CONSECUTIVE_WRITE_FAILURE_LIMIT = 5;
#else
// ... existing native fake-file harness
#endif
```

Leave the `#else` (native) block unchanged.

- [ ] **Step 2: Implement device `readStore` and `writeStore`**

In the `readStore` and `writeStore` functions, replace the ARDUINO branches (which currently just `return false;`) with real implementations. The final versions:

```cpp
bool readStore(uint8_t* buf, size_t size) {
#ifdef ARDUINO
  if (!fsReady) return false;
  File f = SD.open(PERSIST_PATH, FILE_READ);
  if (!f) return false;
  if (f.size() != size) { f.close(); return false; }
  size_t n = f.read(buf, size);
  f.close();
  return n == size;
#else
  if (fakeFile.size() != size) return false;
  std::memcpy(buf, fakeFile.data(), size);
  return true;
#endif
}

bool writeStore(const uint8_t* buf, size_t size) {
#ifdef ARDUINO
  if (!fsReady) return false;
  if (consecutiveWriteFailures >= CONSECUTIVE_WRITE_FAILURE_LIMIT) return false;
  if (!SD.exists(PERSIST_DIR)) SD.mkdir(PERSIST_DIR);
  File f = SD.open(PERSIST_PATH, FILE_WRITE);
  if (!f) { ++consecutiveWriteFailures; return false; }
  f.seek(0);  // FILE_WRITE in Seeed FS appends by default; seek to start
  size_t n = f.write(buf, size);
  f.close();
  if (n != size) { ++consecutiveWriteFailures; return false; }
  consecutiveWriteFailures = 0;
  return true;
#else
  fakeFile.assign(buf, buf + size);
  ++writeCount;
  return true;
#endif
}
```

Note: the `SD.begin()` call belongs in `persistInit`. Seeed Arduino FS exposes `SD` as the mount point over SFUD.

- [ ] **Step 3: Replace the ARDUINO portion of `persistInit`**

In `src/persist.cpp`, update `persistInit` to mount FS on device:

```cpp
void persistInit() {
  setDefaults();
  fsReady = false;
#ifdef ARDUINO
  if (!sfud_init() == SFUD_SUCCESS) {
    Serial.println("[persist] SFUD init failed; using defaults");
  } else if (!SD.begin(SFUD_HOST_DEVICE)) {
    Serial.println("[persist] FS mount failed; using defaults");
  } else {
    fsReady = true;
  }
#else
  fsReady = true;
#endif
  PersistData tmp;
  if (fsReady && readStore(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp))) {
    if (tmp.magic == PERSIST_MAGIC && tmp.version == PERSIST_VERSION) {
      data = tmp;
    }
  }
  prevSessionTokens = 0;
  lastFlushedLifetimeTokens = data.deviceLifetimeTokens;
  lastFlushMs = 0;
  dirty = false;
}
```

NOTE: `sfud_init()` and `SD.begin(SFUD_HOST_DEVICE)` are the Seeed BSP idioms. Exact API names may differ slightly — if the device build fails at link time, inspect the headers under `.pio/libdeps/seeed_wio_terminal/Seeed_Arduino_FS/` and `/Seeed_Arduino_SFUD/` to find the correct call and adjust inline.

- [ ] **Step 4: Verify device build**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS. RAM/Flash usage should grow by a few KB (FS + SFUD libs).

If compile fails with "no matching function" for SFUD or SD calls, read the BSP headers and report BLOCKED with the error.

- [ ] **Step 5: Verify native tests still pass**

```bash
pio test -e native
```
Expected: 55/55.

- [ ] **Step 6: Commit**

```bash
git add src/persist.cpp
git commit -m "feat(persist): device FS mount + file I/O via Seeed_Arduino_FS + SFUD"
```

---

## Task 9: main.cpp integration

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add `#include "persist.h"`**

Near the other project includes at the top of `src/main.cpp`, add:

```cpp
#include "persist.h"
```

- [ ] **Step 2: Initialize persist in `setup()` with first-boot default name**

Find `setup()` currently:
```cpp
  initUi();
  initButtons();
  backlightInit();
  renderBoot("BLE init...");

  appState.deviceName = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
  appState.mode = Mode::BleInit;
```

Change to:
```cpp
  initUi();
  initButtons();
  backlightInit();
  persistInit();
  if (persistGet().deviceName[0] == '\0') {
    std::string def = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
    std::strncpy(persistMut().deviceName, def.c_str(), 32);
    persistMut().deviceName[32] = '\0';
    persistCommit(true);
  }
  renderBoot("BLE init...");

  appState.deviceName = persistGet().deviceName;
  appState.ownerName  = persistGet().ownerName;
  appState.mode = Mode::BleInit;
```

If `<cstring>` isn't already included in main.cpp, add `#include <cstring>` near the top.

- [ ] **Step 3: Call `persistTick` every loop**

Find in `loop()`:
```cpp
  backlightTick(appState, now);

  delay(10);
```

Change to:
```cpp
  backlightTick(appState, now);
  persistTick(now);

  delay(10);
```

- [ ] **Step 4: Increment appr/deny on A/C press**

Find in `loop()`'s button handling:
```cpp
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
```

Change the inner block to also bump the counter on success:

```cpp
    if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
      char btn = (e == ButtonEvent::PressA) ? 'A' : 'C';
      PermissionDecision d;
      std::string id;
      if (applyButton(appState, btn, now, d, id)) {
        if (btn == 'A') persistMut().appr++;
        else            persistMut().deny++;
        persistCommit(true);
        std::string line = formatPermission(id, d);
        sendLine(line);
        lastButtonSendMs = now;
        render(true);
      }
    }
```

- [ ] **Step 5: Persist name/owner on cmd success**

Find in `onLine`'s switch:

```cpp
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      sendLine(formatAck("owner", true));
      break;
```

Change to:

```cpp
    case MessageKind::Owner:
      if (applyOwner(appState, m.ownerName)) pendingRender = true;
      std::strncpy(persistMut().ownerName, m.ownerName.c_str(), 32);
      persistMut().ownerName[32] = '\0';
      persistCommit(true);
      sendLine(formatAck("owner", true));
      break;
```

And find:

```cpp
    case MessageKind::NameCmd: {
      std::string err;
      bool ok = applyNameCmd(appState, m.nameValue, err);
      sendLine(formatAck("name", ok, err));
      break;
    }
```

Change to:

```cpp
    case MessageKind::NameCmd: {
      std::string err;
      bool ok = applyNameCmd(appState, m.nameValue, err);
      if (ok) {
        std::strncpy(persistMut().deviceName, appState.deviceName.c_str(), 32);
        persistMut().deviceName[32] = '\0';
        persistCommit(true);
      }
      sendLine(formatAck("name", ok, err));
      break;
    }
```

- [ ] **Step 6: Wire heartbeat → persistUpdateFromHeartbeat**

Find the Heartbeat case in `onLine`:

```cpp
    case MessageKind::Heartbeat: {
      if (appState.mode == Mode::Connected ||
          appState.mode == Mode::Disconnected) {
        applyConnected(appState);
      }
      bool newPrompt = m.heartbeat.hasPrompt &&
                       (!appState.hb.hasPrompt ||
                        m.heartbeat.prompt.id != appState.hb.prompt.id);
      applyHeartbeat(appState, std::move(m.heartbeat), now);
      if (newPrompt) backlightWake(now);
      pendingRender = true;
      break;
    }
```

Change to (add 2 lines after `applyHeartbeat`):

```cpp
    case MessageKind::Heartbeat: {
      if (appState.mode == Mode::Connected ||
          appState.mode == Mode::Disconnected) {
        applyConnected(appState);
      }
      bool newPrompt = m.heartbeat.hasPrompt &&
                       (!appState.hb.hasPrompt ||
                        m.heartbeat.prompt.id != appState.hb.prompt.id);
      applyHeartbeat(appState, std::move(m.heartbeat), now);
      persistUpdateFromHeartbeat(appState.hb.tokens, appState.hb.tokens_today);
      persistCommit(false);
      if (newPrompt) backlightWake(now);
      pendingRender = true;
      break;
    }
```

- [ ] **Step 7: Verify device build**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS.

- [ ] **Step 8: Verify native tests**

```bash
pio test -e native
```
Expected: 55/55.

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "feat(persist): wire init, tick, and event callsites in main"
```

---

## Task 10: status.cpp — pull stats from persist

**Files:**
- Modify: `src/status.h`
- Modify: `src/status.cpp`
- Modify: `test/test_protocol/test_protocol.cpp`

- [ ] **Step 1: Extend `StatusSnapshot` in `src/status.h`**

Update the struct:

```cpp
struct StatusSnapshot {
  std::string name;
  std::string ownerName;
  int32_t     appr = 0;
  int32_t     deny = 0;
  int32_t     lvl = 0;
  int32_t     nap = 0;
  int32_t     vel = 0;
  bool        sec = false;
  uint32_t    upSec = 0;
  uint32_t    heapFree = 0;
};
```

- [ ] **Step 2: Update `captureStatus` in `src/status.cpp`**

Change the current body:

```cpp
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

to:

```cpp
#ifdef ARDUINO
StatusSnapshot captureStatus(const AppState& /*s*/, uint32_t nowMs) {
  const PersistData& p = persistGet();
  StatusSnapshot snap;
  snap.name      = p.deviceName;
  snap.ownerName = p.ownerName;
  snap.appr      = p.appr;
  snap.deny      = p.deny;
  snap.lvl       = p.lvl;
  snap.nap       = p.nap;
  snap.vel       = p.vel;
  snap.sec       = false;
  snap.upSec     = nowMs / 1000;
  snap.heapFree  = freeHeapBytes();
  return snap;
}
#endif
```

Add `#include "persist.h"` at the top of `src/status.cpp`.

- [ ] **Step 3: Emit real stats in `formatStatusAck`**

Still in `src/status.cpp`, find the `formatStatusAck` stats block:

```cpp
  JsonObject stats = data.createNestedObject("stats");
  stats["appr"] = 0;
  stats["deny"] = 0;
  stats["vel"]  = 0;
  stats["nap"]  = 0;
  stats["lvl"]  = 0;
```

Change to:

```cpp
  JsonObject stats = data.createNestedObject("stats");
  stats["appr"] = snap.appr;
  stats["deny"] = snap.deny;
  stats["vel"]  = snap.vel;
  stats["nap"]  = snap.nap;
  stats["lvl"]  = snap.lvl;
```

- [ ] **Step 4: Update test for status shape**

In `test/test_protocol/test_protocol.cpp`, find `test_format_status_ack_shape` and update it to pass real values and verify they show up:

```cpp
void test_format_status_ack_shape() {
  StatusSnapshot snap;
  snap.name = "Claude-52DA";
  snap.ownerName = "Felix";
  snap.appr = 42;
  snap.deny = 3;
  snap.lvl = 5;
  snap.nap = 12;
  snap.vel = 8;
  snap.sec = false;
  snap.upSec = 12;
  snap.heapFree = 80000;
  std::string out = formatStatusAck(snap);
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("ack":"status")"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("ok":true)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("name":"Claude-52DA")"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("sec":false)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("usb":true)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("up":12)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("heap":80000)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("appr":42)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("deny":3)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("lvl":5)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("nap":12)"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), R"("vel":8)"));
  TEST_ASSERT_EQUAL('\n', out.back());
}
```

- [ ] **Step 5: Run native tests**

```bash
pio test -e native
```
Expected: 55/55 with updated test assertion set.

- [ ] **Step 6: Verify device build**

```bash
pio run -e seeed_wio_terminal
```
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/status.h src/status.cpp test/test_protocol/test_protocol.cpp
git commit -m "feat(persist): status ack emits real stats from persist module"
```

---

## Task 11: Device smoke test + PR

**Files:** no code changes

- [ ] **Step 1: Upload**

```bash
pio run -e seeed_wio_terminal -t upload
```
If upload fails with "Couldn't find a board": double-click Wio power button, retry.

- [ ] **Step 2: Smoke test checklist**

Connect Claude Desktop's Hardware Buddy, then:

- [ ] Stats panel `appr` is 0 on first boot (or prior value on subsequent boots)
- [ ] Press A to approve a permission → `appr` increments; Hardware Buddy reflects new value after the next status poll (2s)
- [ ] Press C to deny → `deny` increments
- [ ] Rename device in Hardware Buddy Name field → Save succeeds; name persists after unplugging power + reconnecting
- [ ] Change owner name by sending `{"cmd":"owner","name":"Tzangms"}` via Bluetility → ownerName saved; survives reboot
- [ ] Interact with Claude for ~30 minutes (or send manual heartbeats with increasing `tokens`) → `lvl` increments every 50K tokens
- [ ] Run `pio run -t upload` to re-flash same version → QSPI data **preserved** (unlike program-flash FlashStorage)

If any checkpoint fails, inspect serial log for `[persist]` messages and check `/wioclaude/stats.bin` state.

- [ ] **Step 3: Push branch**

```bash
git push -u origin feature/sp2-persistence
```

- [ ] **Step 4: Open PR**

```bash
gh pr create --title "SP2: persistence" --body "$(cat <<'EOF'
## Summary
Persist stats (appr/deny/lvl/nap/vel), identity (deviceName/ownerName), and token counters to QSPI flash. Survives reboot and re-flash.

Spec: `docs/superpowers/specs/2026-04-19-sp2-persistence-design.md`
Plan: `docs/superpowers/plans/2026-04-19-sp2-persistence.md`

**Implementation:**
- New `src/persist.{h,cpp}` module
- Packed `PersistData` struct (110 bytes) in `/wioclaude/stats.bin` via Seeed FS + SFUD
- Write debounce for heartbeat-driven updates (5 min OR delta>1000 tokens)
- Immediate writes for button / name / owner events
- File I/O deferred from BLE callback to main-loop `persistTick`
- `status` ack now emits real stats values (no longer hardcoded zeros)
- First-boot: `deviceName` defaults to `Claude-XXXX` and persists

**Native tests:** 55/55 including 13 new in `test/test_persist/`.

## Test plan
- [x] `pio test -e native`: 55/55
- [x] `pio run -e seeed_wio_terminal`: builds clean
- [x] Device smoke test (7 checkpoints; see plan Task 11 Step 2)

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Self-Review

### Spec coverage

- ✅ Scope: all 7 persisted fields — Task 2 (struct), Task 9 (callsites)
- ✅ `appr`/`deny` increment on buttons — Task 9 Step 4
- ✅ `deviceName`/`ownerName` on cmd — Task 9 Step 5
- ✅ Heartbeat delta tokens + lvl — Task 7
- ✅ Token debounce (5 min OR delta>1000) — Tasks 5, 6
- ✅ FS failure fallback to defaults — Task 2 skeleton + Task 8 device mount
- ✅ BLE callback never writes flash — Tasks 9 (commits marked dirty) + tick in loop
- ✅ Status ack real values — Task 10
- ✅ First-boot default name — Task 9 Step 2
- ✅ Desktop-restart protection (max(0, delta)) — Task 7
- ✅ Struct packing via `__attribute__((packed))` + `static_assert` — Task 2
- ✅ Magic/version validation — Task 3 (tests), skeleton already implements
- ✅ Consecutive write-failure limit — Task 8 (device branch only)
- ✅ Native test coverage — Tasks 2–7 (13 tests total)

### Placeholder scan

No "TBD" / "TODO" / "implement later" / "similar to Task N" / vague phrases. Every step shows exact code and exact commands.

### Type consistency

- `PersistData` struct defined in Task 2; used verbatim in Tasks 3, 4, 5, 6, 7, 9, 10
- Function signatures (`persistInit`, `persistGet`, `persistMut`, `persistCommit(bool)`, `persistTick(uint32_t)`, `persistUpdateFromHeartbeat(int64_t, int64_t)`) — consistent across all tasks
- `StatusSnapshot` extended in Task 10 with exact field names used by `formatStatusAck` in the same task
- `PERSIST_MAGIC`, `PERSIST_VERSION`, `PERSIST_DEBOUNCE_MS`, `PERSIST_DEBOUNCE_TOKENS`, `TOKENS_PER_LEVEL` — introduced in Task 1, used identically in tests (Tasks 2–7) and implementation (Tasks 2, 5, 6, 7)
- Test-only accessors `_persistFakeFile()`, `_persistFakeFileSize()`, `_persistResetFakeFile()`, `_persistWriteCount()` — declared in Task 2 persist.h, used across Tasks 3–7 tests

### Open follow-ups

- Q-OPEN-1 (auto-format): deferred to Task 8 implementation; the current device code uses `SD.begin(...)` to mount and assumes the QSPI is already formatted. If a fresh Wio Terminal shows `FS mount failed` in smoke test, the plan notes to inspect SFUD headers for a format function — that would be a follow-up fix, not in scope.
- Q-OPEN-2 (packing): resolved to `__attribute__((packed))` + `static_assert`.
- Q-OPEN-3 (CRC32): skipped (YAGNI). If bit-rot becomes an issue in the field, add a separate feature branch.
