#pragma once

#include <cstdint>
#include "pet.h"

// Called once at boot after manifestLoadActiveFromPersist. Validates that
// the active manifest's first-variant file for each required state opens
// as a readable GIF. Cheap (header-only open+close) — not a full decode.
// Sets characterReady(). Safe when manifestActive() is null (no-op).
void characterInit();

// Whether SP6b's renderer is active. False → caller renders ASCII.
bool characterReady();

// Notify the renderer that the display's state machine has moved to
// `state`. Closes the currently-open GIF (if any), resets variant timers,
// and opens the new state's first-variant file. Idempotent if state is
// unchanged.
void characterSetState(PetState state);

// Drive one animation tick. Call every loop iteration while the Idle
// screen is visible. Handles per-frame timing, end-of-animation rotation
// for multi-variant idle, and the between-variant pause window.
void characterTick(uint32_t nowMs);

// UI called fullRedraw on the Idle screen — the buddy region was wiped.
// Next characterTick will reopen and repaint.
void characterInvalidate();

#ifndef ARDUINO
// Test-only: expose the pure pick-file logic and let tests drive time
// without building a real AnimatedGIF pipeline.
const char* _characterPickFile(PetState state, uint32_t nowMs);
void _characterResetForTest();
#endif
