#pragma once

#include <cstdint>
#include <cstddef>

// --- BLE ---
static constexpr const char* NUS_SERVICE_UUID =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char* NUS_RX_UUID =
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr const char* NUS_TX_UUID =
    "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static constexpr const char* DEVICE_NAME_PREFIX = "Claude-";

// --- Timeouts (ms) ---
static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 30000;
static constexpr uint32_t BACKLIGHT_IDLE_MS    = 30000;
static constexpr uint32_t ACK_DISPLAY_MS       = 1000;
static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 20;
static constexpr uint32_t POST_SEND_LOCKOUT_MS = 500;

// --- Display (320x240 landscape after setRotation(3)) ---
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 240;

// --- Colors (RGB565) ---
static constexpr uint16_t COLOR_BG         = 0x0000;  // black
static constexpr uint16_t COLOR_FG         = 0xFFFF;  // white
static constexpr uint16_t COLOR_DIM        = 0x7BEF;  // grey
static constexpr uint16_t COLOR_OK         = 0x07E0;  // green
static constexpr uint16_t COLOR_WARN       = 0xFD20;  // orange
static constexpr uint16_t COLOR_ALERT_BG   = 0xC000;  // red
static constexpr uint16_t COLOR_ALERT_TEXT = 0xFFFF;
static constexpr uint16_t COLOR_FOOTER_BG  = 0x2104;  // near-black grey

// --- Backlight ---
static constexpr uint8_t BACKLIGHT_FULL = 255;
static constexpr uint8_t BACKLIGHT_DIM  = 50;

// --- SP1 protocol completeness ---
static constexpr size_t ENTRIES_MAX      = 5;
static constexpr size_t ENTRY_CHARS_MAX  = 128;
static constexpr size_t NAME_CHARS_MAX   = 32;
static constexpr size_t STATUS_ACK_BUF   = 512;
