#pragma once

#include <cstddef>

struct AppState;

enum class PetState {
  Sleep,
  Idle,
  Busy,
  Attention,
};

static constexpr size_t PET_FACE_LINES = 4;

PetState petComputeState(const AppState& s);

// Returns a pointer to PET_FACE_LINES const char* rows, each up to 8 chars.
const char* const* petFace(PetState state);
