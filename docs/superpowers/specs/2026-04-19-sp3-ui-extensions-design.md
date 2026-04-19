# SP3 UI Extensions Design

> **Autonomous mode note:** This spec was written without interactive brainstorming (user asleep, authorized "按照推薦"). Decisions are called out inline for later review.

## 背景

SP1 + SP2 讓 firmware 拿到 heartbeat 完整欄位（`entries` / `tokens_today` / `lvl` / `ownerName`）並持久化 stats，但 Idle 畫面目前只顯示 total/running/waiting 三個大數字 + 一行 msg + owner footer。其他欄位全沒顯示。SP3 擴展 Idle screen 讓使用者看得到更多 reference 視覺資訊。

## 目標

Idle 畫面加上：

1. **Level badge**：顯示 `L{persistGet().lvl}`（例如 `L3`）
2. **Tokens today**：顯示 `{tokens_today}t`，若 ≥1000 用千位簡化（例如 `8.2kt`）
3. **Transcript entries**：最多 3 行，取 `AppState.hb.entries[0..2]`

## 非目標

- ❌ B 鍵 / scroll transcript（下一次視覺更新）
- ❌ Menu 系統（下一個 sub-project，風險較大）
- ❌ Screen cycling（A 鍵已是 approve，cycling 衝突）
- ❌ Animated elements（留給 SP4）
- ❌ 新模組、新 state、新持久化欄位

## 架構

**僅改 `src/ui.cpp::renderIdle`**。簽名不變（`const AppState&, bool fullRedraw`）。

- 讀資料：`s.hb.entries` / `s.hb.tokens_today` / `persistGet().lvl`
- 加 `#include "persist.h"` 到 ui.cpp
- `renderIdle` 內部新增：
  - fullRedraw 路徑：draw 新區塊的 labels / box frames
  - 每次呼叫：清對應 rect 然後重畫（跟現有 number cell 策略一致）

### 新版 Idle Layout（320×240 landscape）

```
y  0-28   Header "Claude Buddy" [connected dot]
y 32-46   Small stats row: "L{lvl}   {tokens_today}t"
y 50-60   Labels: "Total  Running  Waiting"
y 65-110  Big numbers (size 5)
y 120-135 msg line
y 145-215 Transcript entries (3 lines, size 1, dim colour)
y 218-240 Owner footer (unchanged)
```

### 渲染細節

**Level + Tokens row（新）**：
```cpp
if (fullRedraw) {
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(8, 36);
  tft.print("Level");
  tft.setCursor(SCREEN_W - 120, 36);
  tft.print("Tokens today");
}
// Always update (tokens_today changes each heartbeat):
tft.fillRect(8, 46, 60, 14, COLOR_BG);
tft.setTextColor(COLOR_FG, COLOR_BG);
tft.setTextSize(2);
tft.setCursor(8, 46);
char lvlBuf[8];
snprintf(lvlBuf, sizeof(lvlBuf), "L%d", persistGet().lvl);
tft.print(lvlBuf);

tft.fillRect(SCREEN_W - 120, 46, 112, 14, COLOR_BG);
tft.setCursor(SCREEN_W - 120, 46);
char tokBuf[12];
int64_t t = s.hb.tokens_today;
if (t < 1000)       snprintf(tokBuf, sizeof(tokBuf), "%d t", (int)t);
else if (t < 100000) snprintf(tokBuf, sizeof(tokBuf), "%.1f kt", t / 1000.0);
else                 snprintf(tokBuf, sizeof(tokBuf), "%d kt", (int)(t / 1000));
tft.print(tokBuf);
```

**Big numbers**：Y 座標從 65 調整到 65（不變），但 label row 從 y=50 保留。

**Transcript（新）**：
```cpp
// Clear the transcript area each call to avoid ghost text
tft.fillRect(0, 145, SCREEN_W, 70, COLOR_BG);
tft.setTextColor(COLOR_DIM, COLOR_BG);
tft.setTextSize(1);
size_t n = std::min(s.hb.entries.size(), (size_t)3);
for (size_t i = 0; i < n; ++i) {
  tft.setCursor(8, 148 + (int)i * 22);
  tft.print(s.hb.entries[i].c_str());
}
```

Entries 最長 128 字（SP1 parser 截過）；螢幕寬度只能容約 50 字，超出會被 TFT driver 自然裁掉或換行。接受。

### msg line 位置微調

原本 `msg` 畫在 y=160-180 區域；因為 transcript 佔了 y=145-215，把 msg 移到 y=120-135（接在 big numbers 下面）。

原本：
```cpp
tft.fillRect(0, 160, SCREEN_W, 20, COLOR_BG);
tft.setCursor(8, 165);
```

改成：
```cpp
tft.fillRect(0, 120, SCREEN_W, 20, COLOR_BG);
tft.setCursor(8, 125);
```

## 錯誤處理

| 狀況 | 處理 |
|---|---|
| `persistInit` 失敗、lvl 為 0 | 顯示 `L0`（合理） |
| `hb.entries` 空 | Transcript 區塊清成黑（已 `fillRect`）|
| `tokens_today` 為 0 | 顯示 `0 t`（沒問題） |
| `lvl` 很大（例如 12 位數） | `%d` 可表示；不特殊處理 |

## 測試策略

- **Native**：ui.cpp 不在 native build filter，無 unit test。SP3 不加。
- **Device**：build compile + 使用者回報（autonomous mode skip，留給 user wake 時驗）
- **Regression**：MVP A permission 流程、SP1 entries 解析、SP2 持久化都不動 → 應不退步

## 風險

- 畫面佔用從 ~50% 變成 ~90%，視覺可能擁擠。接受。
- `persistGet()` 呼叫從 ui.cpp 進行 —— ui.cpp 被 device build 獨占（不在 native），所以依賴 persist 不會破壞 native tests。
- `tokens_today` 千位簡化公式（`< 100000` 用 1 位小數、≥ 100000 去小數）為估算可讀性，未經使用者確認，可微調。

## 依賴

- SP1 (main)、Idle-off (main)、SP2 (main)
- 無後續 SP 依賴此
