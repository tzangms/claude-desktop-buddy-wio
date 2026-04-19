#pragma once

#include <cstdint>
#include <cstddef>

struct AppState;

enum class PetState {
  Sleep,
  Idle,
  Busy,
  Attention,
};

static constexpr size_t PET_FACE_LINES = 4;
static constexpr size_t PET_FRAMES_PER_STATE = 3;

PetState petComputeState(const AppState& s);

// Rows for a given state and frame index (0..PET_FRAMES_PER_STATE-1).
const char* const* petFace(PetState state, size_t frameIdx);

// Animation tick. Returns true if the frame index advanced.
bool petTickFrame(uint32_t nowMs);

size_t petCurrentFrame();

// Snap frame to 0 and anchor the tick clock.
void petResetFrame(uint32_t nowMs);
