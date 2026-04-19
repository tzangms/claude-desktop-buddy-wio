#pragma once

struct AppState;

enum class PetState {
  Sleep,
  Idle,
  Busy,
  Attention,
};

PetState petComputeState(const AppState& s);

// Returns a multi-line ASCII face; lines separated by '\n'. 4 lines, up to 8 chars each.
const char* petFace(PetState state);
