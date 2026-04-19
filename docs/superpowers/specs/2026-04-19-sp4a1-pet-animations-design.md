# SP4a.1 Pet Animations Design

> **Autonomous mode.** Following user authorization "照推薦不要問我".

## 背景

SP4a 做了 4 個 static 寵物面孔。Reference 專案每個 state 有多個 frame 輪替，讓寵物感覺「活著」（例如 idle 眨眼、busy 動眼球）。SP4a.1 加最小的 multi-frame 動畫。

## 目標

1. 每個 PetState 有 3 個 frame
2. Frame cycling 速率 ~500 ms/frame
3. State 改變時立刻切到新 state 的 frame 0（不等 tick）
4. ui.cpp 的 pet cache 擴展成 (state, frame) pair — state 或 frame 任一變就 redraw，兩者同時穩定則跳過

## 非目標

- ❌ 多 species / species 選擇
- ❌ Celebrate / Dizzy / Heart states（仍在 SP4b.2）
- ❌ GIF 或 folder push（SP4b.4）
- ❌ 每個 state 不同 frame 數（統一 3）

## 架構

### config.h 新常數
```cpp
static constexpr uint32_t PET_FRAME_MS = 500;
```

### pet.h API 變動
```cpp
static constexpr size_t PET_FRAMES_PER_STATE = 3;

// Face now takes state + frame.
const char* const* petFace(PetState state, size_t frameIdx);

// Advance the animation frame if PET_FRAME_MS has elapsed since last tick.
// Returns true if frame index changed (caller should trigger re-render).
bool petTickFrame(uint32_t nowMs);

// Current frame index; also reset when petResetFrame() is called.
size_t petCurrentFrame();

// Call when the pet STATE changes (not the frame). Resets frame to 0 and
// anchors the tick clock so the new state's frame 0 shows instantly.
void petResetFrame(uint32_t nowMs);
```

### pet.cpp
- 4 arrays of `const char* const[PET_FRAMES_PER_STATE][PET_FACE_LINES]`
- Private state: `size_t frameIdx = 0; uint32_t lastFrameMs = 0;`
- `petTickFrame`: if `nowMs - lastFrameMs >= PET_FRAME_MS`, bump `frameIdx = (frameIdx + 1) % PET_FRAMES_PER_STATE`, set `lastFrameMs = nowMs`, return true

### Face content

**Sleep** (subtle breathing):
```
frame 0: | z |
frame 1: | . |
frame 2: | z |
```

**Idle** (blink):
```
frame 0: (o o)
frame 1: (- -)
frame 2: (o o)
```

**Busy** (mouth wiggle):
```
frame 0: | ~ |
frame 1: | - |
frame 2: | = |
```

**Attention** (alert punch):
```
frame 0: | ! |
frame 1: |!!!|
frame 2: | ! |
```

Only middle 2 rows differ between frames; top/bottom fixed for each state.

### main.cpp integration

```cpp
// loop() — right after backlightTick:
if (petTickFrame(now)) {
  pendingRender = true;
}
```

State change detection: renderIdle already caches `lastPet`. When state changes, it naturally triggers redraw. To reset the frame clock on state change, ui.cpp (or state transition site in main) calls `petResetFrame(now)` — wait, ui.cpp can do it inline since it already detects state change:

```cpp
PetState st = petComputeState(s);
size_t frame = petCurrentFrame();
if (st != lastPet) {
  petResetFrame(now);  // new state snaps to frame 0
  frame = 0;
  // ...redraw pet face (existing code)...
  lastPet = st;
  lastFrame = frame;
} else if (frame != lastFrame) {
  // ...redraw pet face...
  lastFrame = frame;
}
```

But renderIdle signature doesn't take `now`. Two options:
a. Add `uint32_t now` param to renderIdle (and to render())
b. Have pet module hold its own `lastFrameMs` and ignore the tick window when petResetFrame runs; then ui.cpp just calls petResetFrame() with 0 or any value — but that'd break the 500ms spacing

Cleaner: add `uint32_t nowMs` param to `renderIdle`. Propagate from render(). main.cpp's `render(true)` call site already has `now`.

**Decision**: expand `renderIdle` signature to take `(const AppState&, uint32_t nowMs, bool fullRedraw)`. Other renderers unchanged.

### ui.cpp changes

- Pet cache becomes `static PetState lastPet = ...; static size_t lastFrame = ~0U;`
- Pet render block: if state or frame changed → redraw

## 測試

Native tests for pet tick logic:
- `test_tick_no_advance_within_window` — tick at `< PET_FRAME_MS` → no change
- `test_tick_advances_at_window` — tick at `>= PET_FRAME_MS` → frame++, returns true
- `test_tick_wraps` — after 3 ticks, frame back to 0
- `test_reset_resets_frame` — `petResetFrame(now)` → `petCurrentFrame() == 0`
- `test_face_3_frames_non_null` — each state has 3 valid rows arrays

## 風險

- R1: 500ms/frame × 12 frames = 2Hz pet update; pet render is 80×32 = 2560 px fillRect + 4 short prints. ~3-5 ms SPI. At 2Hz that's 0.6-1% SPI duty. OK.
- R2: Frame changes fire `pendingRender = true` → triggers full `render()` → iterates switch → `renderIdle(...)` → ui.cpp checks caches and only pet block redraws. Each heartbeat saves ~30ms; each frame tick costs ~5ms. Net still positive.
- R3: If user dislikes the choreography, frame art in pet.cpp is trivial to edit.

## 依賴
- SP4a (main @ a64b451 has the pet module + simplify sweep)
