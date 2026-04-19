#pragma once

#include <cstdint>
#include <cstddef>

struct AppState;

enum class PetState {
  Sleep,
  Idle,
  Busy,
  Attention,
  Celebrate,
  Heart,
};

static constexpr size_t PET_FACE_LINES = 4;
static constexpr size_t PET_FRAMES_PER_STATE = 3;

// Compute current pet state. Time-limited overrides (Celebrate / Heart)
// take priority over mode-derived states until they expire.
PetState petComputeState(const AppState& s, uint32_t nowMs);

// Rows for a given state and frame index.
const char* const* petFace(PetState state, size_t frameIdx);

// Animation tick.
bool petTickFrame(uint32_t nowMs);
size_t petCurrentFrame();
void petResetFrame(uint32_t nowMs);

// Time-limited state overrides. Each sets an expiry so petComputeState
// will return the given state until nowMs passes the expiry.
void petTriggerCelebrate(uint32_t nowMs);
void petTriggerHeart(uint32_t nowMs);
