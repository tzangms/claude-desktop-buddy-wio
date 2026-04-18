# Wio Terminal Claude Buddy — MVP A 設計

## 背景

[`anthropics/claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy) 是 Anthropic 官方提供的 BLE maker 參考實作,跑在 M5StickC Plus (ESP32) 上。本專案把同一套 BLE 協議移植到 **Wio Terminal** (ATSAMD51 + RTL8720DN),讓持有 Wio Terminal 的使用者可以接收 Claude Desktop 的 heartbeat、在裝置上批准 / 拒絕 permission prompt。

完整 BLE 協議定義於原 repo 的 [`REFERENCE.md`](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md),本專案不依賴原 repo 的程式碼,只遵循協議。

## 目標(MVP A)

1. BLE 連上 Claude Desktop(macOS / Windows,需開啟 Developer Mode)
2. 顯示 heartbeat 的 total / running / waiting / msg
3. 當 heartbeat 帶 `prompt` 時,進入 prompt 畫面,按鈕可批准或拒絕
4. 斷線重連能自動處理

## 非目標(明確排除)

- ❌ ASCII 寵物動畫(原版 `buddies/`)
- ❌ GIF 角色包(原版 `character.cpp` + folder push)
- ❌ Transcript 捲動(原版的 `entries` 陣列只顯示最新一行)
- ❌ Tokens / 升級慶祝(`tokens_today`、level up)
- ❌ NVS 持久化 stats(原版 `stats.h`)
- ❌ 裝置端選單(設定、重置、換寵物)

這些都是之後 B/C/D 階段再做。

## 架構

### 硬體依賴

- **Wio Terminal**(Seeed Studio):ATSAMD51 主 MCU,RTL8720DN 副 MCU 負責 WiFi/BLE,透過 SPI eRPC 通訊
- **前置條件**:RTL8720DN 韌體必須先更新到支援 `rpcBLE` 的版本(`ambd_flash_tool` 或 Seeed wiki 指示的 sketch)。這一步會在 README 清楚記錄,沒做 BLE 不會動。

### 軟體層

| 層級 | 函式庫 | 職責 |
|---|---|---|
| BLE | `Seeed_Arduino_rpcBLE` | Nordic UART Service、廣播、RX write、TX notify |
| 顯示 | `Seeed_Arduino_LCD`(TFT_eSPI fork) | 320×240 畫面繪製 |
| JSON | `ArduinoJson` v6 | heartbeat 解析、permission 序列化 |
| 按鈕 | 純 `digitalRead` + 20ms debounce | KEY A/B/C 邊緣觸發 |

**資源使用**:ATSAMD51 有 192KB RAM。JSON 緩衝 4KB、顯示 frame 透過 TFT_eSPI 串流繪製(無 full framebuffer),整體使用遠低於上限。

### 廣播設定

- 名稱:`Claude-Wio-XXXX`,其中 `XXXX` 是 BT MAC 後 4 bytes 的 hex
- 廣播 Nordic UART Service UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`

### 協議實作範圍

**接收(desktop → 裝置,write on NUS RX)**

| 訊息 | 處理 |
|---|---|
| heartbeat snapshot(`total`/`running`/`waiting`/`msg`/`prompt`/`entries`/...) | 更新 `AppState`,依 `prompt` 欄位切換畫面 |
| `{"time":[epoch,offset]}` | 同步 RTC(MVP 僅做 backlight timeout 計時,不顯示時鐘) |
| `{"cmd":"owner","name":"..."}` | 存名字,顯示於 idle 畫面底部 |
| `{"evt":"turn",...}` | 忽略(MVP 不做 transcript) |
| folder push / xfer | 不支援 |

**發送(裝置 → desktop,notify on NUS TX)**

僅一種:
```json
{"cmd":"permission","id":"<prompt.id>","decision":"once"|"deny"}
```

**斷線偵測**:30 秒沒收到 heartbeat → 視為斷線,rpcBLE 處理重連。

## UI 佈局

螢幕 320×240 橫向。三個畫面,純 `TFT_eSPI` 繪製,無 sprite、無 double buffer。

### 畫面 1:Idle

```
┌──────────────────────────────────────┐
│ Claude Buddy            ● connected  │  header 24px
├──────────────────────────────────────┤
│                                      │
│    Total   Running   Waiting         │  標籤 16px
│     3         1         0            │  數字 48px
│                                      │
├──────────────────────────────────────┤
│  working on login flow...            │  20px (顯示 msg 欄位)
├──────────────────────────────────────┤
│ Hi, Felix                            │  20px
└──────────────────────────────────────┘
```
- 背景黑,前景白
- 連線綠 / 斷線灰的狀態點
- 30 秒無互動 → backlight 降至 20%,任何按鈕或 heartbeat 事件升回 100%

### 畫面 2:Permission Prompt

```
┌──────────────────────────────────────┐
│  ⚠  PERMISSION REQUESTED             │  header 紅底白字
├──────────────────────────────────────┤
│    Tool: Bash                        │
│                                      │
│    rm -rf /tmp/foo                   │  hint 自動換行
│                                      │
├──────────────────────────────────────┤
│  [C] Deny            [A] Allow once  │  底部按鈕提示
└──────────────────────────────────────┘
```
- 進入時 backlight 強制 100%
- `hint` 超過顯示寬度時截斷(加 `…`),不捲動
- 按下後顯示「✓ Approved」或「✗ Denied」1 秒,回 Idle

### 畫面 3:Disconnected

中央大字 `Disconnected`,下方小字 `scanning...`。

## 按鈕對應

Wio Terminal 頂部按鈕由左到右標示 **C / B / A**。

| 按鈕 | Idle | Prompt | Disconnected |
|---|---|---|---|
| **KEY A**(右) | — | **Allow once** | — |
| **KEY B**(中) | — | — | — |
| **KEY C**(左) | — | **Deny** | — |
| **5-way Press** | 喚醒 backlight | 喚醒 backlight | — |

**設計理由**:右手食指 = approve(正向),左手食指 = deny,符合人因。KEY B 中間留空防誤觸。5-way 方向鍵 MVP 保留,之後做 transcript 捲動用。

**Debounce**:20 ms software debounce,按下邊緣觸發。
**送出後抑制**:送出 permission decision 後 500 ms 禁用按鈕,避免同一 heartbeat 循環誤觸兩次。

## 狀態機

```
BOOT → BLE_INIT → ADVERTISING → CONNECTED
                        ▲                │
                        │                ▼
                  DISCONNECTED ← IDLE ⇄ PROMPT
                                        │
                                        ▼
                                       ACK → IDLE (1s)
```

| 轉換 | 條件 |
|---|---|
| BLE_INIT → ADVERTISING | rpcBLE 初始化成功 |
| BLE_INIT → FATAL | rpcBLE 初始化失敗(螢幕顯示錯誤碼) |
| ADVERTISING → CONNECTED | central 連線事件 |
| CONNECTED → IDLE/PROMPT | 收到第一個 heartbeat,依 `prompt` 欄位決定 |
| IDLE → PROMPT | heartbeat 帶 `prompt` |
| PROMPT → IDLE | heartbeat 無 `prompt` |
| PROMPT → ACK | 按下 KEY A 或 C,送出 permission decision |
| ACK → IDLE | 顯示回饋 1 秒後自動 |
| IDLE/PROMPT → DISCONNECTED | `now - lastHeartbeatMs > 30000` |
| DISCONNECTED → ADVERTISING | rpcBLE 斷線事件觸發 |

### 關鍵細節

- **CONNECTED 看 heartbeat 才換畫面**:連線不等於協議握手完成
- **PROMPT 期間只 care `prompt` 欄位**:其他欄位更新只寫 state,不換畫面,避免 prompt 畫面被其他資料打斷
- **PROMPT id 切換**:如果新 heartbeat 帶不同 `prompt.id`(舊的被撤回、新的來了),直接更新畫面內容,不走 ACK flash
- **Permission decision 是 fire-and-forget**:不等 desktop 回應,送完立刻進 ACK

## 專案結構

```
wioclaude/
├── platformio.ini          # board=seeed_wio_terminal, lib_deps 鎖版本
├── README.md               # 含 RTL8720 韌體更新步驟
├── src/
│   ├── main.cpp            # setup/loop, state machine dispatch
│   ├── ble_nus.h/.cpp      # rpcBLE 包裝:廣播、RX callback、TX write
│   ├── protocol.h/.cpp     # JSON parse / serialize
│   ├── state.h/.cpp        # AppState struct, transition 函式
│   ├── ui.h/.cpp           # 三個畫面的繪製
│   ├── buttons.h/.cpp      # 3 顆按鈕 debounce + edge detection
│   └── config.h            # 常數:UUID、timeout、顏色、字型大小
└── docs/
    └── superpowers/specs/2026-04-18-wio-claude-buddy-design.md
```

### 模組邊界

```
main.cpp
  ├─▶ ble_nus  (init、設 RX callback)
  ├─▶ buttons  (poll)
  ├─▶ state    (transition)
  └─▶ ui       (render)

ble_nus  ──▶ protocol   (parse 收到的 line)
protocol ──▶ state      (寫入 AppState)
ui       ──▶ state      (讀 AppState)
main     ──▶ protocol   (serialize permission decision)
```

**關鍵約束**:
- `state.h` 不依賴其他應用模組,是純 data struct + transition 函式,好單元測試
- `ui.cpp` 只讀 `AppState`,不改 state(render 無副作用)
- `protocol.cpp` 只負責 JSON 和 state,不碰 BLE

## 測試策略

硬體在手上,大多驗證還是跑真機;但純邏輯模組留可測介面:

- **`protocol`**:parse / serialize 不依賴 Arduino framework,可在 host 上用 PlatformIO `native` 環境跑單元測試(用 ArduinoJson 的 desktop build)
- **`state`**:transition 函式純邏輯,同上可單元測試
- **`ble_nus` / `ui` / `buttons`**:依賴硬體,以真機煙霧測試

## 風險與已知坑

1. **RTL8720DN 韌體版本**:`rpcBLE` 對韌體版本敏感,使用者若未更新 firmware,連廣播都不會起來。README 必須第一步強調。
2. **rpcBLE 連線穩定性**:社群回報 rpcBLE 在高頻 notify 時偶有掉包。MVP 只送小 payload(< 100 bytes 的 permission JSON),理論上不會踩到。
3. **MTU**:NUS notify 在 BLE 5.0 MTU 協商後通常是 247 bytes。heartbeat 的大 payload(含 `entries`)可能要跨多個 notify — 必須在 RX callback 累積 buffer 直到 `\n`。這點照原 `REFERENCE.md` 實作。
4. **沒有 IMU / shake 功能**:MVP 不用,但未來加寵物的 dizzy 狀態時需要 `Seeed_Arduino_LIS3DHTR`。

## 後續版本展望(非本 spec 範圍)

- **B**:加 `entries` 的 transcript 捲動(5-way 上下)、`buddies/` 的 ASCII 寵物移植(橫向重排)
- **C**:NVS stats、tokens 計數、升級慶祝、裝置端選單
- **D**:GIF 角色包的橫向版本(`character.cpp` + `prep_character.py` 重做)

每個階段都會先獨立 spec 再實作。
