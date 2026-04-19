# Deferred work: SP4b.3 / SP4b.4 / SP5

> Written during autonomous execution; stopping here rather than push through
> work whose risk profile calls for the user in the loop. Each entry below
> is a self-contained starting point, not a full spec.

## SP4b.3 — Menu system (factory reset)

**Why deferred**: needs button long-press detection added to `buttons.cpp`, a new `Mode::FactoryResetConfirm`, a confirmation renderer, and a reset path (`NVIC_SystemReset()` after clearing persist). UX choices (trigger gesture, confirmation flow) are easier to verify with the user observing the device.

**Suggested approach**:
- `buttons.cpp`: extend `Btn` struct with `pressedSinceMs`; emit `ButtonEvent::LongPressNav` after `BUTTON_LONG_PRESS_MS` (2000 ms) of continuous 5-way press.
- `state.h`: add `Mode::FactoryResetConfirm`. Store `Mode` to return to on cancel.
- `state.cpp`: add `applyLongPressNav(s)` → transitions to `FactoryResetConfirm` from any non-Prompt mode; `applyConfirmReset(s, button, out)` returns true if A confirmed, sets out.
- `ui.cpp`: `renderFactoryResetConfirm()` shows a warning screen with "Hold A to confirm" style.
- `main.cpp`: on confirmation, call `persistFactoryReset()` (new helper in persist.cpp that zeroes struct + writes + flushes) then `NVIC_SystemReset()`.
- Tests: native coverage for button long-press debounce and state transitions.

**Scope estimate**: ~150 lines across 4 files + 5-6 new tests. One session with a user in the loop.

## SP4b.4 — GIF character packs + folder push transport

**Why deferred**: this is the single biggest feature in the reference project. It requires:

1. **Folder push transport**: `char_begin` / `file` / `chunk` (base64) / `file_end` / `char_end` commands with ack handshake
2. **Base64 decode on the RX path** (incremental, since payloads are chunked)
3. **File writes to QSPI FatFS** (reusing SP2's Seeed_Arduino_FS); safe path validation (reject `..`, absolute paths)
4. **manifest.json parsing** (character name, state→file mapping)
5. **GIF decoder** — reference uses `bitbank2/AnimatedGIF` library; memory-constrained decode on SAMD51 is a real engineering problem (96px × ~140px frames)
6. **Character storage lifecycle**: 1.8 MB cap, delete on `Settings → delete char`, fallback to ASCII when absent
7. **Pet module extension**: load character at boot if present; switch `petFace` source from ASCII arrays to decoded GIF frames

**Scope estimate**: at least 3 dedicated SPs worth of work. Needs: dedicated design round, real hardware iteration on BLE transfer reliability (chunked writes during heartbeats), and GIF decoder memory tuning.

**Entry point**: start with the transport only (no GIF decode), validate BLE folder push works by having it dump bytes to serial. Then add FatFS write. Then add manifest parse. Then worry about decoding. Each is a merge-worthy step.

## SP5 — LE Secure Connections bonding

**Why deferred**: this is high risk. `rpcBLE` on Wio Terminal runs BLE on the RTL8720DN co-processor; enabling LESC bonding requires:

1. Marking NUS characteristics (and the TX CCCD) as encrypted-only
2. Setting IO capability to DisplayOnly and generating a 6-digit passkey
3. Handling pairing callbacks to show the passkey on the TFT
4. Storing LTK (long-term key) to persist across reboots — likely via rpcBLE's bond database
5. Implementing `{"cmd":"unpair"}` to actually call into rpcBLE to erase bonds (currently SP1's `unpair` is a no-op ack)
6. Advertising `sec: true` in status ack once encrypted

**Why high risk**:
- Misconfigured LESC can lock out existing paired devices with no easy recovery path
- If the passkey display or bond storage breaks, the user can't re-pair without firmware recovery
- `rpcBLE` documentation on LESC bonding is sparse; API likely differs from the Nordic reference the protocol assumes

**Suggested approach** (with user in loop):
- First verify `rpcBLE` actually supports LESC: look for `setSecurityCallbacks`, `setEncryption`, `BLESecurity` in the library headers.
- If supported: start with a side-branch that only prints passkey to serial (no actual encryption) to validate the callback flow.
- Only after that works, enable characteristic encryption and test on a second device to avoid locking the primary.
- Implement `unpair` as the final step.

**Scope estimate**: 1-2 SPs, but each requires a second Wio Terminal (or willingness to re-flash aggressively) for testing.

---

## Completed in this autonomous run

- SP4a.1 — multi-frame pet animations (merged #7)
- SP4b.2 — celebrate + heart states with triggers from main.cpp (merged #8)
- SP4b.1 — dizzy + nap states (state machine only; IMU deferred) (merged #9)

72/72 native tests passing on `main @ af507e9`. All device builds clean.

## Not completed (user should confirm on wake)

- **Device smoke test** for every merged PR this session (SP1 through SP4b.1) — zero hardware verification performed autonomously. If anything breaks end-to-end, the first diagnostic step is to reflash and inspect serial for `[persist]` / `[BLE]` / `[mem]` output.
- **SP4b.1 IMU wiring** — the state machine is ready; call `petTriggerDizzy(now)` on shake and `petEnterNap()` / `petExitNap()` on orientation changes. Needs `Seeed_Arduino_LIS3DHTR` in lib_deps.
- **SP4b.3 / SP4b.4 / SP5** — see above.
