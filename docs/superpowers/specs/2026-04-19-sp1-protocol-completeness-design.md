# SP1 — 協定完整性設計

## 背景

MVP A 已經跑通 BLE 連線、heartbeat 的 `total/running/waiting/msg/prompt` 解析、以及 A/C 鍵的 permission 回覆（commit `c646878`）。但對比 [`anthropics/claude-desktop-buddy/REFERENCE.md`](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md) 的完整協定，還缺多個 cmd、event、heartbeat 欄位。

完整 parity 太大，已切成五個子專案（SP1–SP5）。本 spec 聚焦 **SP1：把所有協定訊息的 parse 路徑與必要 ack 補齊**，作為後續所有功能（UI 擴展、持久化、動畫、安全性）的地基。

## 目標

讓 firmware 能**正確辨識並回應** reference 協定定義的所有訊息類型，無論是否在 Wio 螢幕上顯示。驗收條件：

1. Claude Desktop（developer mode）連上後，Hardware Buddy 視窗的 stats 面板持續更新（uptime / heap）
2. Serial log 可看到 heartbeat 的 `entries` / `tokens` / `tokens_today` 被正確 parse
3. Desktop 送 `status` / `name` / `unpair` / `owner` 四個 cmd 都收到對應 ack
4. `pio test -e native` 全綠，涵蓋所有新增純邏輯
5. MVP A 原有 approve/deny 流程不退步

## 非目標

SP1 **不**包含以下，全部屬於其他 SP：

- ❌ Wio 螢幕 UI 任何改動（含 tokens 顯示、transcript 捲動、owner 名字顯示）→ SP3
- ❌ 任何持久化（NVS / QSPI Flash）→ SP2
- ❌ runtime 改 BLE advertising 名字 → SP2（需持久化配合）
- ❌ LE Secure Connections bonding、加密 characteristic、passkey 顯示 → SP5
- ❌ IMU 手勢、user LED 控制、裝置端菜單 → SP3
- ❌ ASCII / GIF 角色動畫、folder push transport → SP4

## 協定對應

下表列出 reference 協定所有訊息類型，以及 SP1 各自的處理策略。

### 接收路徑（device ← desktop）

| 訊息 | 形式 | SP1 行為 | 回覆 |
|---|---|---|---|
| Heartbeat snapshot | 無 `cmd`、無 `evt`，有 `total` | 既有解析 + 新欄位（`entries`/`tokens`/`tokens_today`）存入 `HeartbeatData` | 無（heartbeat 不 ack） |
| Turn event | `{"evt":"turn",...}` | 辨識為 `TurnEvent`、**丟棄**（不存不顯示） | 無（event 不 ack） |
| Time sync | `{"time":[epoch, offset]}` | 存 `timeEpoch` / `timeOffsetSec` / `timeSetAtMs` 到 AppState | 無（非 cmd） |
| Status cmd | `{"cmd":"status"}` | `captureStatus()` → `formatStatusAck()` → `sendLine` | Status ack（含 name/sec/bat/sys/stats） |
| Name cmd | `{"cmd":"name","name":"..."}` | 更新記憶體中 `AppState.deviceName`；空字串或長度 0 拒絕；>32 字截斷 | `{"ack":"name","ok":true}` 或 `{"ack":"name","ok":false,"error":"empty name"}` |
| Owner cmd | `{"cmd":"owner","name":"..."}` | 既有 `applyOwner` 更新 AppState | `{"ack":"owner","ok":true}` |
| Unpair cmd | `{"cmd":"unpair"}` | 無 bonding 可清，直接成功 ack | `{"ack":"unpair","ok":true}` |
| 未知 cmd | `{"cmd":"其他"}` | Serial log，不 ack | 無 |
| JSON 錯誤 / 超量 | 無法解析 | Serial log | 無 |

### 傳送路徑（device → desktop）

| 訊息 | 觸發 | 格式 |
|---|---|---|
| Permission decision | A/C 鍵按下 + 有 pending prompt | 既有 `formatPermission`（不動） |
| Ack (generic) | 收到任何已知 cmd | `formatAck(cmd, ok, error?)` |
| Status ack | 收到 `status` cmd | `formatStatusAck(StatusSnapshot)` |

### Status ack 資料策略

Wio Terminal 沒電池、SP1 不做持久化，因此回傳值如下（設計決議：完整欄位 + 假資料，優先相容 desktop UI 顯示）：

```json
{
  "ack": "status",
  "ok": true,
  "data": {
    "name": "<AppState.deviceName>",
    "sec": false,
    "bat": { "pct": 100, "mV": 5000, "mA": 0, "usb": true },
    "sys": { "up": <millis()/1000>, "heap": <free heap bytes> },
    "stats": { "appr": 0, "deny": 0, "vel": 0, "nap": 0, "lvl": 0 }
  }
}
```

- `name`：目前 `AppState.deviceName`，初始為 `Claude-XXXX`，可被 `name` cmd 改（只影響此欄，不影響實際 advertising）
- `sec`：固定 `false`，SP5 才改
- `bat`：全部假值，表示「滿電、USB 在線、無充放」。SP2 或未來硬體若加電池才改
- `sys.up`：`millis() / 1000`（49 天會 wrap，接受）
- `sys.heap`：SAMD51 free heap（實作見風險 R2）
- `stats`：全 0，SP2 持久化後才帶真值

## 架構

### 檔案切分

```
src/
  ble_nus.{cpp,h}     不動
  buttons.{cpp,h}     不動
  config.h            加 STATUS_ACK_BUF_SIZE、ENTRIES_MAX、ENTRY_CHARS_MAX、NAME_CHARS_MAX
  main.cpp            擴充 onLine dispatch 新 MessageKind
  protocol.{cpp,h}    擴充 MessageKind、新 formatter (formatAck / formatStatusAck)
  state.{cpp,h}       AppState 加 deviceName / timeEpoch / timeOffsetSec / timeSetAtMs;
                      HeartbeatData 加 entries / tokens / tokens_today;
                      applyNameCmd / applyTime
  status.{cpp,h}      新檔:captureStatus(AppState&, uint32_t nowMs) → StatusSnapshot
```

### 分層原則：為什麼拆出 `status.cpp`

`protocol.cpp` 必須可在 `[env:native]` 環境單元測試，不能依賴 `millis()` 或平台相關 heap API。因此把「資料收集」（平台相關）跟「序列化」（純邏輯）分成兩層：

- **`status.cpp`（平台層）**：`captureStatus(const AppState&, uint32_t nowMs) → StatusSnapshot` — 讀 `millis()`、free heap、device name 組成純 struct
- **`protocol.cpp`（純邏輯層）**：`formatStatusAck(const StatusSnapshot&) → std::string` — ArduinoJson 序列化，native 可測

### Data flow

```
BLE RX bytes
  └→ ble_nus.cpp (buffer until '\n')
       └→ main.cpp::onLine(std::string line)
            └→ protocol.cpp::parseLine(line) → ParsedMessage
                 └→ switch (m.kind):
                      Heartbeat  → applyHeartbeat     + pendingRender
                      Owner      → applyOwner         + sendLine(formatAck("owner", true))
                      Time       → applyTime          (no render)
                      StatusCmd  → captureStatus      → formatStatusAck → sendLine
                      NameCmd    → applyNameCmd       + sendLine(formatAck("name", ok, err))
                      UnpairCmd  →                      sendLine(formatAck("unpair", true))
                      TurnEvent  → drop
                      Unknown    → Serial.log
                      ParseError → Serial.log
```

### 關鍵 struct 擴充

```cpp
// state.h 新增欄位
struct AppState {
  // ...existing...
  std::string deviceName;       // 初始 "Claude-XXXX";name cmd 可更新(記憶體內)
  int64_t     timeEpoch = 0;    // 最近收到的 epoch
  int32_t     timeOffsetSec = 0;
  uint32_t    timeSetAtMs = 0;  // 透過 (millis() - timeSetAtMs) 推算當前時間
};

// protocol.h HeartbeatData 新增欄位
struct HeartbeatData {
  // ...existing...
  std::vector<std::string> entries;   // 最多 ENTRIES_MAX 筆(預設 5)
  int64_t tokens = 0;
  int64_t tokens_today = 0;
};

// status.h 新檔
struct StatusSnapshot {
  std::string name;
  bool        sec = false;
  uint32_t    upSec;
  uint32_t    heapFree;
  // bat / stats 由 formatStatusAck 寫死,不進 snapshot
};
```

## 錯誤處理與邊界

### Parser 層

| 狀況 | 處理 |
|---|---|
| JSON 語法錯誤 | `MessageKind::ParseError` → main.cpp serial log → 丟棄 |
| JSON 超過 `StaticJsonDocument<2048>` | ArduinoJson 回 `NoMemory` → 當 ParseError 處理 |
| 有 `cmd` 但值未知（如 `"cmd":"reboot"`） | `MessageKind::Unknown` → serial log → 不 ack |
| `{"evt":"turn",...}` | `MessageKind::TurnEvent` → 丟棄不 ack |
| heartbeat `entries` >5 筆 | 只存前 5 筆 |
| `entries` 單則 >128 字 | 截斷到 128 字 |
| heartbeat 無 `entries` 欄位 | vector 留空（非錯誤） |
| `name` cmd 帶空字串 | 不更動 AppState → 回 `ok:false,error:"empty name"` |
| `name` cmd >32 字 | 截斷到 32 字後接受 → `ok:true` |

### Status ack 產生

| 狀況 | 處理 |
|---|---|
| `AppState.deviceName` 為空（初始化前 poll） | `"name":""` 合法，不崩 |
| 剛開機 uptime 很小 | `up: 0` 正常 |
| Heap API 回 0 | 接受（不會崩，只是顯示 0） |

### 時序與狀態

| 狀況 | 處理 |
|---|---|
| 斷線重連 | `deviceName` 保留（記憶體）；`timeEpoch` 保留，`timeSetAtMs` 過時但下次 sync 會更新 |
| 未連上時收到 `status` cmd | 不可能走到（GATT 連上才能 write）；defensive log 後略過 |
| 重複收 `owner` cmd | 每次更新 + ack，不去重 |
| `deviceName` 改了但廣播名不改 | **設計決議**，非 bug |

### Backward-compat 保證

| MVP A 行為 | 保證方式 |
|---|---|
| A/C 鍵 → permission | `applyButton`、`formatPermission` 不動 |
| Heartbeat → `Mode::Prompt` | `applyHeartbeat` 既有邏輯保留，新欄位只加 |
| prompt id 變化 re-render | `lastRenderedPromptId` 機制保留 |
| `Mode::Ack` timeout | `applyTimeouts` 不動 |
| 斷線 → Advertising | main.cpp conn transitions 不動 |

## 測試策略

### Native 單元測試（`pio test -e native`）

`platformio.ini` 的 `[env:native]` `build_src_filter` 加入 `protocol.cpp` + `state.cpp`（已包含）+ 新 `status.cpp` 中 `formatStatusAck` 的部分（`captureStatus` 排除，因為依賴平台）。

新增 / 擴充測試檔：

| 檔 | 測什麼 |
|---|---|
| `test_protocol_status.cpp`（新） | `formatStatusAck` 正確序列化 `name/sec/sys.up/sys.heap`；`bat` 假資料固定；`stats` 全 0 |
| `test_protocol_ack.cpp`（新） | `formatAck("owner", true)` / `formatAck("name", false, "empty name")` 輸出正確 JSON |
| `test_protocol_parse.cpp`（擴充） | `parseLine` 辨識 `StatusCmd` / `NameCmd` / `UnpairCmd` / `TurnEvent`；heartbeat 含 `entries`/`tokens` 時解析正確；`entries` 超過 5 筆截斷；單則超 128 字截斷；空 `name` cmd 行為 |
| `test_state_name.cpp`（新） | `applyNameCmd` 空字串不更新；正常字串更新 `deviceName`；>32 字截斷 |
| `test_state_time.cpp`（新） | `applyTime` 更新 epoch / offset / setAtMs |

覆蓋目標：protocol.cpp + state.cpp 新增程式碼 100% 有 test 覆蓋。

### Device 煙霧測試（手動）

`pio run -t upload`，用 nRF Connect / Bluetility 連上 NUS，依序送入下列字串（每行 `\n` 結尾）：

1. `{"cmd":"status"}` → 預期 status ack
2. `{"cmd":"name","name":"TestBuddy"}` → 預期 `{"ack":"name","ok":true}`
3. `{"cmd":"name","name":""}` → 預期 `{"ack":"name","ok":false,"error":"empty name"}`
4. `{"cmd":"unpair"}` → 預期 `{"ack":"unpair","ok":true}`
5. `{"cmd":"owner","name":"Tzangms"}` → 預期 `{"ack":"owner","ok":true}`
6. `{"evt":"turn","role":"assistant","content":[{"type":"text","text":"hi"}]}` → 預期無 ack（只 serial log）
7. `{"total":1,"running":1,"waiting":0,"msg":"busy","entries":["10:42 a","10:41 b"],"tokens":50000,"tokens_today":3000}` → 預期 Prompt/Idle 既有行為不變、serial 印出 entries/tokens

### 整合驗收（真正的 Claude Desktop）

1. `Help → Troubleshooting → Enable Developer Mode`
2. `Developer → Open Hardware Buddy…` → Connect 到 `Claude-XXXX`
3. 檢查 stats 面板：
   - ✅ uptime 持續增加
   - ✅ heap 顯示合理數字
   - ✅ name 顯示 `Claude-XXXX`
   - ⚠️ bat 顯示滿電 USB（假資料，符合設計）
   - ⚠️ stats 全 0（符合設計）
4. 跟 Claude 做一次 Bash 之類需要 permission 的對話 → A 鍵核准 → 驗證 MVP A 流程不退步

## 風險與開放問題

### R1 — `onLine` callback 同步 sendLine

`onLine` 執行在 rpcBLE callback context（main.cpp 既有註解就說「keep it cheap, defer SPI to loop()」）。SP1 在 callback 中直接 `sendLine` 回 ack 可能跟內部狀態互鎖。

- **緩解**：先照同步寫（reference 實作也是同步 ack）。若煙霧測試看到 hang / dropped 回覆，改成將 ack 排入 main loop 的 queue 非同步送。
- **影響**：中等（可能需要 Plan 階段加 pending-ack queue）

### R2 — SAMD51 的 free heap API

Wio Terminal 主 MCU 是 SAMD51（ARM Cortex-M4F），跑 Arduino framework，**不是 FreeRTOS**（FreeRTOS 在副 MCU RTL8720DN 上）。`xPortGetFreeHeapSize()` 在主 MCU 不適用。

- **需要**：SBRK-based `freeMemory()` helper（用 `__brkval` / `&_ebss` / stack pointer 計算），或找 Seeed BSP 現成 API
- **驗證時機**：Plan 階段第一個 step 先做 spike，確認 heap 數字合理
- **影響**：高（沒這個則 `sys.heap` 永遠是 0 或不準確）

### R3 — Heartbeat JSON 體積膨脹

加入 `entries` + `tokens` + `tokens_today` 後，典型 heartbeat payload 可能超過 `StaticJsonDocument<2048>`。`entries` 5 筆 × 128 字 ≈ 640 字，加上其他欄位仍應在 2KB 內，但 desktop 實際送多大尚未量測。

- **緩解**：Plan 階段量測實際 payload；必要時調到 4096 或改 `DynamicJsonDocument`
- **影響**：中（parse 失敗會讓顯示凍結在舊 state）

### R4 — `millis()` wrap

49 天後 `millis()` 回繞，`sys.up` 會歸零。

- **影響**：低（maker 裝置不會連續開機這麼久），不處理

### R5 — `[env:native]` 編譯範圍

`status.cpp` 同時含 `captureStatus`（平台依賴）與可 native 測的 helper。如果整個檔丟進 native build 會失敗。

- **緩解**：用 `#ifdef` 或把 `captureStatus` 移到 `status_device.cpp` 另開檔；Plan 階段決定
- **影響**：低

### Q-OPEN-1 — Seeed BSP 有沒有 `freeMemory()`？

Plan 階段第一個 spike：查 Seeed_Arduino_FS / rpcBLE / LCD 這幾個 lib 有沒有 helper；沒有就自己寫。

### Q-OPEN-2 — `sendLine` 超過 MTU 如何切包？

Status ack 約 200+ bytes，超過預設 BLE MTU（~20 bytes）。`ble_nus.cpp` 的 outbound 路徑目前怎麼處理？Plan 階段 Read 一次確認（如果是 rpcBLE 自動切包就不用改；若需自己 chunk，要加邏輯）。

### Q-OPEN-3 — 假 bat 值抽 config 常數？

微優化。Plan 階段決定寫死在 `formatStatusAck` 還是抽到 `config.h`（SP2 有實際電池時比較好換）。

## 依賴與後續

- **依賴**：無（MVP A 已完成）
- **後續 SP 依賴 SP1**：
  - SP2（持久化）：需 SP1 的 `name`/`owner` 解析路徑
  - SP3（UI 擴展）：需 SP1 的 `entries`/`tokens`/`owner`/`time` 資料
  - SP4（角色動畫）：需 SP1 的 `running`/`waiting`/`tokens` 觸發狀態判斷
  - SP5（安全）：可獨立於 SP1，但 `unpair` cmd 實作會在 SP5 替換掉 SP1 的「假 ack」
