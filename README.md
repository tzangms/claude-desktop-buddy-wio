# Wio Terminal Claude Buddy (MVP A)

A Wio Terminal firmware that connects to Claude Desktop over BLE, shows
heartbeat status, and lets you approve/deny permission prompts with the
top buttons.

Port of the BLE protocol from Anthropic's reference
[`claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy)
(written for M5StickC Plus / ESP32). This is an independent implementation
— only the wire protocol is shared.

## What works (MVP A)

- BLE Nordic UART Service, pairs with Claude Desktop's Hardware Buddy
- Idle screen: total / running / waiting counts + latest `msg`
- Permission prompt screen: tool name, hint, approve (A) / deny (C)
- Auto-reconnect on disconnect

## Not (yet) included

No pets, no GIF character packs, no transcript scrolling, no stats
persistence. See `docs/superpowers/specs/` for the phased plan.

## Hardware

- **Wio Terminal** (Seeed Studio, ATSAMD51 + RTL8720DN)
- USB-C cable

## Prerequisites

### 1. Install PlatformIO

See <https://docs.platformio.org/en/latest/core/installation/>.

### 2. RTL8720DN firmware

The Wio Terminal's BLE runs on a separate Realtek RTL8720DN co-processor.
If your device was shipped with stock Seeed firmware, the included
`rpcBLE` library should work out of the box. If BLE fails to advertise,
you may need to update the RTL8720 firmware using Seeed's `ambd_flash_tool`
— refer to Seeed Studio's Wio Terminal wiki.

### 3. Enable Developer Mode in Claude Desktop

`Help → Troubleshooting → Enable Developer Mode`. Then
`Developer → Open Hardware Buddy…`.

## Flashing

```bash
pio run -e seeed_wio_terminal -t upload
pio device monitor -b 115200
```

To enter bootloader mode if a regular upload fails, **double-tap the
power switch** (left side, slide down) — the green LED should pulse.

## Running tests

Pure-logic modules (`protocol`, `state`) are unit-tested on the host via
PlatformIO's `native` env:

```bash
pio test -e native
```

## Pairing

1. Power on the Wio Terminal — it shows `advertising as Claude-XXXX`
2. In Claude Desktop: Developer menu → Open Hardware Buddy → **Connect**
3. Pick `Claude-XXXX` from the list
4. The screen should change to `connected, waiting...` then the Idle screen
   once the first heartbeat arrives.

## Buttons

| Button | Idle | Prompt |
|---|---|---|
| KEY A (right top) | — | Approve |
| KEY C (left top)  | — | Deny |
| 5-way press       | wake backlight | wake backlight |

## License

MIT (see LICENSE).
