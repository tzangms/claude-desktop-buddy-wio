# SP4a ASCII Pet Design

> **Autonomous mode note:** Written without interactive brainstorming; decisions called out inline.

## 背景

Reference 專案的核心 UX 是 ASCII / GIF 寵物跟著 Claude session 的狀態跳動。SP1-3 已把協定、持久化、Idle 資訊都接上，但螢幕還沒有會動的角色。SP4a 先做 ASCII 寵物最小集合，為 SP4b（GIF + folder push）鋪路。

## 目標（最小集）

1. 新模組 `src/pet.{h,cpp}`：純邏輯 state 計算 + face string 對應
2. 4 種 pet state：**Sleep / Idle / Busy / Attention**
3. 1 species（inline），4 張 static ASCII faces
4. Idle screen 右上角（或專屬區）顯示當前 face
5. State 從 `AppState.mode` + `hb.running` + `hb.hasPrompt` 推導
6. Native 單元測試驗 state 邏輯

## 非目標（SP4a 明確排除）

- ❌ 多 frame 動畫（每 state 只 1 張 face）
- ❌ Celebrate（需 lvl 變化 edge detection — 留給 SP4a.1 或 SP4b）
- ❌ Dizzy（需 IMU — 留給 SP4b 整合 LIS3DHTR）
- ❌ Heart（需 approve < 5s 計時 — 留給之後）
- ❌ 18 種 species 切換、pet menu
- ❌ GIF character packs、folder push transport（SP4b）
- ❌ NVS 存 pet species 偏好（SP4b）

## 架構

### 新檔 `src/pet.h`

```cpp
#pragma once

#include <cstdint>

struct AppState;

enum class PetState {
  Sleep,       // Advertising / Disconnected / BleInit
  Idle,        // Connected, running==0, waiting==0
  Busy,        // Connected, running > 0
  Attention,   // Mode::Prompt
};

// Pure state mapping — native testable.
PetState petComputeState(const AppState& s);

// Return a 4-line ASCII face for a given state.
// Each line fits in 10 chars; lines are '\n'-separated in one const string.
const char* petFace(PetState state);
```

### 新檔 `src/pet.cpp`

```cpp
#include "pet.h"
#include "state.h"

namespace {
  // 4 lines × up to 10 chars each; null-terminated single string.
  const char* FACE_SLEEP     = " ,---.\n (- -)\n | z |\n '---'";
  const char* FACE_IDLE      = " ,---.\n (o o)\n | _ |\n '---'";
  const char* FACE_BUSY      = " ,---.\n (> <)\n | ~ |\n '---'";
  const char* FACE_ATTENTION = " ,---.\n (O O)\n | ! |\n '---'";
}

PetState petComputeState(const AppState& s) {
  switch (s.mode) {
    case Mode::Prompt:
      return PetState::Attention;
    case Mode::Idle:
    case Mode::Ack:
      return s.hb.running > 0 ? PetState::Busy : PetState::Idle;
    case Mode::Connected:
      return PetState::Idle;
    default:
      return PetState::Sleep;
  }
}

const char* petFace(PetState state) {
  switch (state) {
    case PetState::Sleep:     return FACE_SLEEP;
    case PetState::Idle:      return FACE_IDLE;
    case PetState::Busy:      return FACE_BUSY;
    case PetState::Attention: return FACE_ATTENTION;
  }
  return FACE_IDLE;
}
```

### ui.cpp 整合

Add `renderPetFace(const char* face, int x, int y, uint16_t colour)` helper that splits on '\n' and draws each line with `tft.print`.

Call from `renderIdle`:
```cpp
#include "pet.h"
// ...
// at end of renderIdle, top-right corner x=230 y=32, size 2:
PetState st = petComputeState(s);
renderPetFace(petFace(st), 230, 32, COLOR_FG);
```

The pet area is ~80×64 px at text size 2 (10 chars × 12 px wide = 120 px; oh too wide). Use text size 1 (6 px wide): 10 chars × 6 = 60 px wide, 4 lines × 8 = 32 px tall. Fits comfortably at y=32.

### Layout impact on Idle

The existing "tokens today" label sits at `x = SCREEN_W - 120`, y=36. The pet at x=230, y=32 overlaps. Move "tokens today" to a different spot OR shrink pet to left-of-tokens.

**Decision**: place pet at x=8..68, y=40..72 (below header), and shift the "Total"/"Running"/"Waiting" labels + big numbers down by ~32 px. Actually cleaner: move pet to the **right of numbers**.

Revised layout (size-1 pet):
```
y  0-28   Header
y 32-46   Level / Tokens today row
y 50-60   Labels Total / Running / Waiting
y 65-110  Big numbers (as before; move back up)
y 125-150 Pet face, right side (x=260, y=125, 4 lines × 8px = 32px)
y 155     msg line
y 175+    Transcript (kept but shorter)
y 218+    Footer
```

Actually this is getting fiddly. Simplest placement: put pet **to the right of the big numbers**, same row. Big numbers currently span x=38..348 (3 cells × 90px + gaps). No room. 

**Simpler decision**: place pet **at the footer line opposite the owner greeting**. Owner footer is left-justified at x=8, y=SCREEN_H-16. Pet goes at right-justified x=SCREEN_W-64, y=SCREEN_H-40 (above footer).

Actually even simpler — pet bottom-right, but the transcript is in that area. Let me just overlay pet in a previously-unused spot.

**Final decision**: place the pet **at top-right**, x=250, y=32, size-1. That's the "tokens today" label area. **Move tokens today** down next to the level value row.

New top zone:
```
y 32-46   "Level"           (labels, left)            Pet (top-right, 60×32)
y 46-60   "L{lvl}"          (value, left)             
y 66-80   "Tokens today"    (label, centered)
y 82-96   "{tokens} kt"     (value, centered)
y ...     Totals, msg, transcript, footer unchanged
```

Hmm this pushes everything else down. Too many knock-on changes.

**Revised final decision**: put the pet **inside a newly-reserved rectangle at bottom-left**, overlapping footer. Owner greeting moves to bottom-center.

OK I'm over-thinking. Let me just put the pet **above the owner footer**, centered, at y=190..220, replacing part of the transcript. Transcript shrinks from 3 lines to 2.

Final layout:
```
y  0-28   Header
y 32-46   Level label + Tokens today label
y 46-60   L{lvl}              {tokens_today}
y 66-80   "Total" "Running" "Waiting"
y 82-118  Big numbers (moved up)
y 125-140 msg line
y 148-188 Transcript (2 lines now: y=148, y=168)
y 192-216 Pet face, centered x=130, size 1 (4 × 8 = 32px tall)
y 218-240 Owner footer
```

### Pet render helper in ui.cpp

```cpp
// Draws a multi-line ASCII face. face is a '\n'-delimited string.
// Size-1 text means 6px wide per char, 8px tall per line.
void renderPetFace(const char* face, int x, int y, uint16_t colour) {
  tft.setTextColor(colour, COLOR_BG);
  tft.setTextSize(1);
  const char* line = face;
  int row = 0;
  while (line && *line) {
    const char* nl = strchr(line, '\n');
    size_t len = nl ? (size_t)(nl - line) : strlen(line);
    // Print this line
    tft.fillRect(x, y + row * 8, 60, 8, COLOR_BG);  // clear
    tft.setCursor(x, y + row * 8);
    char buf[16];
    size_t copyLen = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, line, copyLen);
    buf[copyLen] = '\0';
    tft.print(buf);
    if (!nl) break;
    line = nl + 1;
    ++row;
  }
}
```

Called from `renderIdle`:
```cpp
PetState st = petComputeState(s);
renderPetFace(petFace(st), 130, 192, COLOR_OK);
```

Colour by state: keep simple — always `COLOR_OK` (green). Attention uses `COLOR_ALERT_BG`. Future can extend.

## 測試

### Native (`test/test_pet/test_pet.cpp`)

```cpp
test_sleep_when_advertising
test_sleep_when_disconnected
test_idle_when_no_running
test_busy_when_running_gt_zero
test_attention_when_prompt_mode
test_face_strings_non_null
```

~6 tests.

### platformio.ini

Add `pet.cpp` to native build filter.

### Device smoke

Skipped in autonomous mode.

## 風險

- R1: Text wraps oddly if terminal font misses monospace alignment. TFT_eSPI default font is 6×8 monospace, safe.
- R2: transcript 從 3 行縮到 2 行，高資訊密度使用者可能不喜歡。接受（微調 trivial）。
- R3: Pet state 目前沒動畫 — 顯示「死的」face 可能反而不可愛。SP4b/a.1 加 frame cycling 修。

## 依賴

- SP1 (main) — AppState.hb fields
- SP2 (main) — persist (read-only, already integrated)
- Idle-off (main)

## 後續 (SP4b 範圍)

- 多 frame 動畫 + swap timer
- Celebrate state (hook `lvl` edge from persist)
- Dizzy (IMU) + Heart (approve timing)
- Multiple species + selection menu
- GIF character packs via folder push transport
