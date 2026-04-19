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
  Dizzy,
  Nap,
};

static constexpr size_t PET_FACE_LINES = 4;
static constexpr size_t PET_FRAMES_PER_STATE = 3;

// Compute current pet state. Time-limited / latched overrides take
// priority over mode-derived states. Priority (high → low):
//   Celebrate > Heart > Dizzy > Nap > mode-derived (Attention/Busy/...).
PetState petComputeState(const AppState& s, uint32_t nowMs);

// Rows for a given state and frame index.
const char* const* petFace(PetState state, size_t frameIdx);

// Animation tick.
bool petTickFrame(uint32_t nowMs);
size_t petCurrentFrame();
void petResetFrame(uint32_t nowMs);

// Time-limited overrides: each sets an expiry in the future.
void petTriggerCelebrate(uint32_t nowMs);
void petTriggerHeart(uint32_t nowMs);
void petTriggerDizzy(uint32_t nowMs);

// Nap is latched: petEnterNap() stays until petExitNap() is called
// (when the device is flipped right-side-up, for example).
void petEnterNap();
void petExitNap();
bool petIsNapping();
