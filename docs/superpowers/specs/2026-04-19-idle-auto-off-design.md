# Idle Auto-Off Backlight Design

## 背景

SP1 的 simplify sweep 把舊版「30 秒 idle 後 `setBacklight(20)`」整包移除，因為 Wio Terminal 的 `LCD_BACKLIGHT` 是 `NOT_ON_PWM`（`.pio/packages/framework-arduino-samd-seeed/variants/wio_terminal/variant.cpp:149`），`analogWrite(51)` fallback 到 `digitalWrite(LOW)` 反而把螢幕整個關掉。removal 後螢幕永遠全亮，長時間放桌上會浪費電與 LCD 壽命。

本 spec 重新實作 idle 省電，但這次明確把行為設計成 **on/off 兩態**（配合硬體能力），並把狀態機獨立成 `backlight` 模組以便測試。

## 目標

1. 連線後閒置 60 秒 → 背光關
2. 按任一鍵（A / B / C / 5-way）→ 背光開、計時重設
3. 收到新 prompt（`hasPrompt` false→true 或 `prompt.id` 變化）→ 背光開、計時重設
4. Prompt mode 中：背光永遠開（approval 必須看得到）
5. 睡著時螢幕內容不變、不重畫（TFT frame buffer 保留）
6. 任何 GPIO 寫入只在 awake 狀態切換時發生（不是每 loop）
7. Native unit test 驗狀態機純邏輯
8. MVP A / SP1 原有行為不退步

## 非目標

- ❌ PWM 漸變 dim（硬體 `NOT_ON_PWM`）
- ❌ 可變 timeout（例如 desktop cmd 改值）— 先寫死在 `config.h`
- ❌ 動態活動偵測（IMU 晃動 / 光感）— SP4 IMU 時再談
- ❌ CPU deep sleep 省電
- ❌ 特殊 sleep 畫面 / icon
- ❌ AppState 或其他既有模組的重構

## 架構

### 檔案新增 / 變更

```
src/
  backlight.h      ← 新:公開 API
  backlight.cpp    ← 新:實作 + 私有狀態
  config.h         ← 加 BACKLIGHT_IDLE_MS,刪 BACKLIGHT_FULL / BACKLIGHT_DIM
  main.cpp         ← init / wake / tick 整合點
  ui.cpp           ← initUi() 不再碰背光 GPIO
test/
  test_backlight/test_backlight.cpp   ← 新
platformio.ini     ← native build_src_filter 加入 backlight.cpp
```

### 模組介面

```cpp
// backlight.h
#pragma once

#include <cstdint>

struct AppState;

// Initialize GPIO, set backlight ON. Call once from setup().
void backlightInit();

// Wake the screen: turn backlight ON, reset idle timer. Idempotent.
void backlightWake(uint32_t nowMs);

// Per-loop tick. May transition awake → sleep when
// (now - lastActivityMs) > BACKLIGHT_IDLE_MS AND state.mode != Mode::Prompt.
void backlightTick(const AppState& s, uint32_t nowMs);

// Query for tests / other consumers. No side effect.
bool backlightIsAwake();
```

私有狀態在 `backlight.cpp` 匿名 namespace 裡：

```cpp
namespace {
  bool awake = true;
  uint32_t lastActivityMs = 0;
}
```

### 為什麼傳 `const AppState&` 給 `backlightTick`

`backlightTick` 只要讀 `state.mode` 判斷是不是 `Prompt`。傳整個 `AppState` 比另開「isPromptActive()」helper 簡單，而且未來若要擴展（例如 Fatal 模式強制亮）不用改介面。

### 為什麼 `awake` 初值 `true`、`lastActivityMs` 初值 `0`

- 開機時螢幕要亮（boot / advertising 訊息要看）→ `awake = true`
- `lastActivityMs = 0` 代表「尚未有任何活動」；開機 60 秒後若使用者從未按鍵、也沒收到 prompt，螢幕會自然睡 → 符合省電設計
- 取代原本設計中考慮的「初始 sentinel 延後首次 sleep」方案

## 狀態機與行為

### `backlightInit()`
```
pinMode(LCD_BACKLIGHT, OUTPUT)
digitalWrite(LCD_BACKLIGHT, HIGH)
awake = true
lastActivityMs = 0
```

### `backlightWake(now)`
```
lastActivityMs = now
if (!awake):
  digitalWrite(LCD_BACKLIGHT, HIGH)
  awake = true
```

**邊緣切換**：只有在 `awake` 從 false→true 時才呼叫 `digitalWrite`。

### `backlightTick(state, now)`
```
if (!awake): return
if (state.mode == Mode::Prompt):
  lastActivityMs = now            // 重設計時,避免退出 Prompt 剛好踩到 timeout
  return
if ((now - lastActivityMs) < BACKLIGHT_IDLE_MS): return
digitalWrite(LCD_BACKLIGHT, LOW)
awake = false
```

**Prompt mode 時重設 `lastActivityMs`** 的原因：若 prompt 持續 2 分鐘後才被解除，使用者回到 Idle 狀態的那一刻計時若已跨越 60s，會馬上睡著，突兀。重設讓 Idle 階段從 0 重跑 60s。

### `backlightIsAwake()`

```
return awake
```

純 getter，供 native test 觀察狀態。

## 整合點（main.cpp）

```cpp
// setup()
backlightInit();                                  // 取代 initUi 內的 analogWrite

// loop() button branch
if (e != ButtonEvent::None) {
  backlightWake(now);
}
if (e == ButtonEvent::PressA || e == ButtonEvent::PressC) {
  // existing permission handling
}

// loop() after applyTimeouts
backlightTick(appState, now);

// onLine() Heartbeat case — detect new prompt
bool prevHasPrompt = appState.hb.hasPrompt;
std::string prevPromptId = appState.hb.prompt.id;
applyHeartbeat(appState, std::move(m.heartbeat), now);
if ((!prevHasPrompt && appState.hb.hasPrompt) ||
    (appState.hb.hasPrompt && appState.hb.prompt.id != prevPromptId)) {
  backlightWake(now);
}
pendingRender = true;
```

偵測「新 prompt 出現」的邏輯放在 main.cpp，不動 `applyHeartbeat` 的介面。三行條件判斷，明確、封閉。

## ui 模組清理

- `initUi()` 裡的 `pinMode(LCD_BACKLIGHT, OUTPUT); analogWrite(LCD_BACKLIGHT, BACKLIGHT_FULL);` 刪掉 → 改由 `backlightInit()` 負責
- `config.h` 的 `BACKLIGHT_FULL`、`BACKLIGHT_DIM` 常數刪除（不再有 caller）
- `config.h` 加 `BACKLIGHT_IDLE_MS = 60000`

## 測試策略

### GPIO 抽象

`digitalWrite` / `pinMode` 是 Arduino 函式，native env 不能用。在 `backlight.cpp` 用 `#ifdef ARDUINO` 分開：

```cpp
#ifdef ARDUINO
#include <Arduino.h>
#include "config.h"
static void writePin(bool high) {
  digitalWrite(LCD_BACKLIGHT, high ? HIGH : LOW);
}
#else
// Native test build
static int writeCount = 0;
static bool lastWritten = true;
static void writePin(bool high) { lastWritten = high; ++writeCount; }
// Test-only accessors (declared in backlight.h behind #ifndef ARDUINO if needed,
// or as free functions for test file to extern)
int _backlightWriteCount() { return writeCount; }
bool _backlightLastWritten() { return lastWritten; }
void _backlightResetTestCounters() { writeCount = 0; lastWritten = true; }
#endif
```

`backlightInit()` 在 native build 也把 counter 歸零（這樣 test 之間乾淨）。

### `platformio.ini` 變更

```ini
[env:native]
build_src_filter = +<state.cpp> +<protocol.cpp> +<status.cpp> +<backlight.cpp>
```

### 測試清單（`test/test_backlight/test_backlight.cpp`）

| Test | 驗什麼 |
|---|---|
| `test_init_starts_awake` | `backlightInit()` 後 `backlightIsAwake()` 為 true |
| `test_tick_without_activity_sleeps_after_timeout` | Idle mode、過 60s → sleep；59.9s → 仍 awake |
| `test_wake_from_sleep_restores` | 先睡著、`backlightWake` 後 awake 恢復 true |
| `test_prompt_mode_never_sleeps` | Prompt mode、過 10 分鐘仍 awake |
| `test_prompt_mode_resets_timer` | Prompt → Idle 轉換後，從 0 重跑 60s 才睡 |
| `test_edge_transition_writes_once` | sleep 狀態下多次 tick，`digitalWrite` 只發生 1 次 |
| `test_wake_idempotent_when_already_awake` | 已 awake 時 wake 不發 GPIO 寫入 |

### Device 煙霧測試（手動）

1. 燒錄 → 連 Claude Desktop
2. 閒置 60+ 秒 → 螢幕關
3. 按任一鍵（A / B / C / 5-way）→ 螢幕亮，內容不變
4. 再閒置 60+ 秒 → 又關
5. 要 Claude 做一個需 permission 的動作 → 螢幕被新 prompt 喚醒
6. 不按核准，讓 prompt 放 2 分鐘 → 螢幕還亮著
7. 按 A 核准 → 螢幕還亮；60 秒後才關

## 風險與開放問題

### R1 — `digitalWrite(LOW)` 可能不完全關背光

某些 LED driver 有基本 bias 電流，GPIO LOW 時 LED 可能微亮。煙霧測試驗。若真發生，選項：
- 接受（微亮也算省電）
- 改用 `pinMode(LCD_BACKLIGHT, INPUT)`（pin 變高阻抗，更徹底切）
- 改用 TFT_eSPI 的 display off 指令（需要確認 ST7789 是否支援 SLPIN）

### R2 — 睡眠期間 TFT 內容漂移

TFT 有內建 RAM，關背光不會清除內容。但 ST7789 若自動進 standby 或接到 reset 訊號可能例外。若實測發現喚醒後畫面異常，在 `backlightWake` 裡加 `render(true)` 強制重畫。

### R3 — Prompt → Idle 切換的 UX

Prompt mode 時 `backlightTick` 不斷重設 `lastActivityMs`。prompt 結束後 Idle 階段從 0 重跑 60s。符合「剛做完 approval 別那麼快關」的直覺。納入設計接受。

### R4 — 新 prompt 偵測邏輯在 main.cpp

若未來重構 `applyHeartbeat` 回傳 enum 而不是 bool，這段 3 行判斷可能變重複。目前接受，SP4 寵物狀態機整合時再統一。

### R5 — 開機後無 activity 就倒數

`awake=true` + `lastActivityMs=0` 讓開機 60s 後若沒人按、沒連上 prompt，螢幕會關。實際測試感覺 OK 就留；若使用者反饋不習慣再改 init 把 `lastActivityMs = UINT32_MAX/2` 延後首次 sleep。

### Q-OPEN-1

`backlight.cpp` 用 `writePin` helper 還是直接呼叫 `digitalWrite`？設計選前者以便 test 抽換。Plan 階段確認沒有額外 overhead（inline 後應等同直接呼叫）。

### Q-OPEN-2

要不要刪 `BACKLIGHT_FULL` / `BACKLIGHT_DIM`？
- `BACKLIGHT_FULL`：`initUi` 改走 `backlightInit` 後無人用 → 刪
- `BACKLIGHT_DIM`：本來就死碼 → 順手刪
Plan 階段納入。

## 依賴

- **前置**：無。SP1 已在 main (`71b662d`)
- **後續**：無。SP2（持久化）、SP4（動畫）不依賴此模組
