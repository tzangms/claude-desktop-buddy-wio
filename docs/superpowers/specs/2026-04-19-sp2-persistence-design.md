# SP2 Persistence Design

## 背景

SP1 讓 firmware 解析所有 reference 協定訊息並回應 `status` ack，但 `status ack.data.stats` 全部回 0（`appr` / `deny` / `lvl` / `nap` / `vel`），`name` 回當前記憶體值、重啟就沒了。SP1 spec 明確把「持久化」放到 SP2。

SP2 把以下欄位存到 QSPI flash，讓重啟、斷電、重連後仍保留：

- Counters：`appr` / `deny` / `lvl` / `nap` / `vel`
- Derived：`deviceLifetimeTokens`（算 `lvl` 用，不對外暴露到 status ack）
- Session 值：`tokens_today`
- Identity：`deviceName` / `ownerName`

## 目標

1. 開機後 `persistGet()` 回上次 flush 過的值；新裝置 → defaults
2. 按 A → `appr++`、立即寫 flash；reboot 後 `appr` 保留
3. 按 C → `deny++`、立即寫 flash
4. `{"cmd":"name","name":"X"}` → 更新 deviceName、立即寫；reboot 後廣播名跟著用
5. `{"cmd":"owner","name":"Y"}` → 更新 ownerName、立即寫
6. Heartbeat 帶 `tokens` → 裝置累積 `deviceLifetimeTokens`、重算 `lvl = deviceLifetimeTokens / 50000`；debounce 寫 flash（**5 分鐘 OR delta >1000**）
7. Heartbeat 帶 `tokens_today` → 更新記憶體、同樣 debounce 寫 flash
8. FS 掛載失敗 / 檔案 corrupt → firmware 仍可運作，log error、用 defaults
9. BLE callback（`onLine`）絕不直接寫 flash — file I/O 全 deferred 到 `loop()` 的 `persistTick`
10. `status` ack 的 `data.stats` 欄位從 `persistGet()` 拿真值
11. `status` ack 的 `data.name` 從 `persistGet().deviceName` 拿
12. Native tests 覆蓋：defaults / magic / version / debounce 邏輯 / lvl delta 計算

## 非目標

- ❌ `tokens_today` 的 midnight reset 邏輯（desktop 會送權威值，我們信它）
- ❌ 多檔案 / key-value 儲存（單 binary struct）
- ❌ 加密 / checksum（YAGNI；magic + version 已足夠辨識 corrupt）
- ❌ 跨版本 migration logic（只預留 `version` 欄位）
- ❌ 動態 file path、hot-reload、多 device 切換
- ❌ IMU 硬體支援（`nap` 欄位會被持久化但 increment 在 SP4）
- ❌ 變更 status ack wire format（只是把假值換真值）

## 架構

### 檔案變動

```
src/
  persist.h       新:公開 API + PersistData struct
  persist.cpp     新:實作 + 私有狀態 + fake file (native)
  config.h        加 PERSIST_MAGIC / PERSIST_VERSION / PERSIST_DEBOUNCE_MS
                     PERSIST_DEBOUNCE_TOKENS / TOKENS_PER_LEVEL
  main.cpp        persistInit 在 setup;persistTick 每 loop;
                  A/C/name/owner/Heartbeat callsites 觸發 persist*
  status.{h,cpp}  captureStatus 改從 persistGet() 拿 stats/name/owner
                  formatStatusAck 收 stats 真值
platformio.ini    native build_src_filter 加 persist.cpp
test/
  test_persist/test_persist.cpp     新
```

### 模組介面 `persist.h`

```cpp
#pragma once

#include <cstdint>

struct PersistData {
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

### 私有狀態（`persist.cpp` 匿名 namespace）

```cpp
namespace {
  PersistData data;
  bool dirty = false;
  uint32_t lastFlushMs = 0;
  int64_t lastFlushedLifetimeTokens = 0;
  int64_t prevSessionTokens = 0;
  bool fsReady = false;
}
```

### config.h 新常數

```cpp
static constexpr uint32_t PERSIST_MAGIC           = 0xC1A7DE42;
static constexpr uint32_t PERSIST_VERSION         = 1;
static constexpr uint32_t PERSIST_DEBOUNCE_MS     = 300000;  // 5 min
static constexpr int64_t  PERSIST_DEBOUNCE_TOKENS = 1000;
static constexpr int64_t  TOKENS_PER_LEVEL        = 50000;
```

### 檔案格式

`/wioclaude/stats.bin`：`sizeof(PersistData)` bytes 的 packed struct。`/wioclaude/` 不存在時 `persistInit` 建立。

Struct 用 `__attribute__((packed))` 確保跨 SAMD51 GCC / native GCC layout 一致（Plan 階段確認實作細節）。

## Data Flow

### 開機

```
setup():
  Serial.begin
  initUi
  initButtons
  backlightInit
  persistInit()
  // First-boot default name fallback
  if (persistGet().deviceName[0] == '\0') {
    std::string n = std::string(DEVICE_NAME_PREFIX) + deviceSuffix();
    strncpy(persistMut().deviceName, n.c_str(), 32);
    persistMut().deviceName[32] = '\0';
    persistCommit(true);
  }
  appState.deviceName = persistGet().deviceName;
  appState.ownerName  = persistGet().ownerName;
  initBle(...)
```

### 主迴圈

```
loop():
  pollBle
  ...(既有邏輯不變)...
  backlightTick
  persistTick(now)
  delay(10)
```

### 事件 → persist 觸發表

| 事件 | 觸發位置 | 呼叫 |
|---|---|---|
| A 鍵核准成功（applyButton 回 true） | main.cpp button branch | `persistMut().appr++; persistCommit(true);` |
| C 鍵拒絕成功 | 同上 | `persistMut().deny++; persistCommit(true);` |
| Name cmd 成功（applyNameCmd 回 true） | main.cpp onLine Name case | `strncpy(persistMut().deviceName, m.nameValue.c_str(), 32); persistMut().deviceName[32]='\0'; persistCommit(true);` |
| Owner cmd | main.cpp onLine Owner case | `strncpy(persistMut().ownerName, m.ownerName.c_str(), 32); persistMut().ownerName[32]='\0'; persistCommit(true);` |
| Heartbeat | main.cpp onLine Heartbeat case（在 applyHeartbeat 之後） | `persistUpdateFromHeartbeat(appState.hb.tokens, appState.hb.tokens_today); persistCommit(false);` |

### 為什麼持久化邏輯放 main.cpp 而不是 state.cpp

state.cpp 要能在 `[env:native]` 跑、不能依賴 platform 層。若在 `applyHeartbeat` 內呼叫 persist，得用 `#ifdef ARDUINO` 守，讓 state.cpp 失去純邏輯性質。

改把 persist 呼叫留在 main.cpp 的 `onLine` case。state.cpp 繼續只管狀態轉換。persist.cpp 自己用 `#ifdef ARDUINO` 分離 file I/O 與純邏輯。

### status ack 接線

`status.h` 加欄位：

```cpp
struct StatusSnapshot {
  std::string name;
  std::string ownerName;    // NEW
  int32_t     appr = 0;     // NEW
  int32_t     deny = 0;     // NEW
  int32_t     lvl = 0;      // NEW
  int32_t     nap = 0;      // NEW
  int32_t     vel = 0;      // NEW
  bool        sec = false;
  uint32_t    upSec = 0;
  uint32_t    heapFree = 0;
};
```

`status.cpp::captureStatus`：

```cpp
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
```

`formatStatusAck` 改用 `snap.appr` / `snap.deny` / 等等替代硬寫的 0。

## 錯誤處理與邊界

### FS / 檔案

| 狀況 | 處理 |
|---|---|
| SFUD init 失敗 | log、fsReady=false、defaults；後續 commit/tick no-op |
| FS mount 失敗 | 同上；不自動格式化（保守） |
| `/wioclaude/` 不存在 | `persistInit` 建立；失敗走 no-op |
| 檔案不存在（新裝置） | defaults + 立即 flush 一份 |
| 檔 size 不等於 `sizeof(PersistData)` | corrupt → defaults |
| magic 不符 | corrupt → defaults |
| version 不符 | SP2 暫簡單處理：defaults；TODO 註解標 future migration hook |
| 寫入失敗 | log、dirty 保持；連續失敗計數 ≥5 跳過該週期避免 log spam |

### 時序

| 狀況 | 處理 |
|---|---|
| `persistCommit(true)` 在 fsReady=false | no-op + log；dirty 仍 true |
| `persistCommit(false)` 頻繁 | 預期；debounce 把守 |
| Desktop restart（`sessionTokens` 倒退） | `max(0, sessionTokens - prevSessionTokens)` 避免 lvl 倒退 |
| 從未連過 desktop | deviceName 空 → setup 寫入 `Claude-XXXX` default |
| `tokens_today` 回 0（新一天） | 直接覆寫；debounce flush |
| lvl 溢位 | 不會發生（需 ~100 兆 tokens） |

### Backward-compat

| SP1 / pre-SP2 行為 | 是否退步 | 如何保 |
|---|---|---|
| A/C permission 流程 | 不變 | applyButton / formatPermission 不動 |
| Heartbeat → Prompt / Idle | 不變 | applyHeartbeat 不動 |
| status ack 欄位 shape | 不變 shape；值變真 | formatStatusAck 介面不變 |
| 斷線重連 | 不變 | persist 跟 BLE 獨立 |
| Idle-off / Prompt flicker fix | 不變 | 不碰 render path |

## 測試策略

### 抽象層

`persist.cpp` 用 `#ifdef ARDUINO` 分：
- Platform 層（ARDUINO-only）：SFUD 初始、`fs.open`/`f.write`/`f.read`
- 純邏輯層：`persistUpdateFromHeartbeat` delta、debounce 判斷、magic/version 驗證

Native build：file I/O 替換成 `static std::vector<uint8_t> fakeFile`，`_persistFakeFile()` 等 accessor 讓 test 檢視。

### Native test suite `test/test_persist/`

| Test | 驗什麼 |
|---|---|
| `test_init_empty_uses_defaults` | fake 空 → defaults（magic/version set、其他 0 / ""） |
| `test_init_loads_existing_data` | fake 塞合法 bytes → persistGet 回對應值 |
| `test_init_rejects_wrong_magic` | magic 錯 → defaults |
| `test_init_rejects_wrong_version` | version 錯 → defaults |
| `test_init_rejects_wrong_size` | file size 短於 struct → defaults |
| `test_commit_immediate_flushes_now` | `commit(true)` 立即 +1 write |
| `test_commit_debounced_waits` | `commit(false); tick(0);` 無 write |
| `test_tick_flushes_after_timeout` | debounced → `tick(5min+1)` 寫 |
| `test_tick_flushes_after_delta_tokens` | delta >1000 → `tick(even at 0)` 寫 |
| `test_tick_skips_when_clean` | 無 dirty → tick 無 write |
| `test_heartbeat_updates_lvl` | `updateFromHeartbeat(50000, 0)` → lvl=1 |
| `test_heartbeat_desktop_restart_no_negative_delta` | prev=10000, now=500 → lifetime 不動、lvl 不動 |
| `test_heartbeat_accumulates_lifetime` | 兩次 heartbeat delta 累加 |

### Device 煙霧

1. 燒錄 → 連 Desktop → Hardware Buddy 看 stats
2. 按 A 幾次 → appr 漲
3. 按 C 幾次 → deny 漲
4. 改 deviceName → 重拔電 → 連回 → name 保留
5. 跟 Claude 互動 30 分鐘 → lvl 隨 tokens 升
6. `pio run -t upload` 同版重燒 → QSPI 資料保留

## 風險與開放問題

### R1 — FS init boot 延遲

Seeed FS + SFUD 初始化可能幾百 ms。若 BLE init 依賴 persist 結果，boot 時間拉長。
**緩解**：目前設計 `persistInit` 在 `initBle` 之前、接受短延遲。若實測 > 1 秒可改順序或非同步 init。

### R2 — QSPI write 阻塞

每次寫 flash 約 10-50ms block。期間 BLE callback 若有事件會堆進 rxBuf（ble_nus.cpp 有 8KB 保護）。
**緩解**：煙霧測試驗；若掉包，改 deferred queue 跨多 loop 寫。

### R3 — Struct packing

`sizeof(PersistData)` 跨 SAMD51 GCC / native GCC 可能有 padding 差異。
**緩解**：用 `__attribute__((packed))` 或手動 static_assert 檢查；Plan 階段實作時確認。

### R4 — Fake file 跟真 FS 行為差異

Test 的 in-memory buffer 不模擬 atomic write / power fail。
**緩解**：device 煙霧測試為 final gate；power-fail 不在 SP2 目標範圍。

### R5 — connected retry spam

若 QSPI 一直寫失敗（硬體問題），log 可能刷屏。
**緩解**：連續失敗 ≥5 次跳過該週期，next commit 重置計數。

### Q-OPEN-1 — 自動格式化策略

FS 未格式化時是否自動 format？保守做法不自動（避免毀資料），但新 device 會一直跑 defaults。Plan 階段決定要不要「首次失敗 → format → retry」。

### Q-OPEN-2 — Struct packing 實作

用 `__attribute__((packed))` vs `#pragma pack`？或手動 offset？Plan 階段確認工具鏈支援與 native / device 一致性。

### Q-OPEN-3 — CRC32 檢查

不加（YAGNI），但 QSPI 單 bit 翻轉會讓 data 變奇怪。Plan 階段判斷是否加 ~4 bytes CRC。

## 依賴

- **前置**：SP1（已 main）、Idle-off（已 main）
- **後續依賴**：
  - SP3（UI 擴展）：可顯示 `persistGet().ownerName` / `appr` / `lvl`
  - SP4（動畫）：celebrate trigger 比對 `lvl` 變化；`nap` counter 由 IMU 事件更新
