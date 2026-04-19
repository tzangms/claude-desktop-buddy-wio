#include "pet.h"
#include "state.h"

namespace {
  const char* const FACE_SLEEP[PET_FACE_LINES] = {
    " ,---.",
    " (- -)",
    " | z |",
    " '---'",
  };
  const char* const FACE_IDLE[PET_FACE_LINES] = {
    " ,---.",
    " (o o)",
    " | _ |",
    " '---'",
  };
  const char* const FACE_BUSY[PET_FACE_LINES] = {
    " ,---.",
    " (> <)",
    " | ~ |",
    " '---'",
  };
  const char* const FACE_ATTENTION[PET_FACE_LINES] = {
    " ,---.",
    " (O O)",
    " | ! |",
    " '---'",
  };
}

PetState petComputeState(const AppState& s) {
  switch (s.mode) {
    case Mode::Prompt:
      return PetState::Attention;
    case Mode::Idle:
    case Mode::Ack:
      return s.hb.running > 0 ? PetState::Busy : PetState::Idle;
    case Mode::Connected:
      return PetState::Idle;
    default:
      return PetState::Sleep;
  }
}

const char* const* petFace(PetState state) {
  switch (state) {
    case PetState::Sleep:     return FACE_SLEEP;
    case PetState::Idle:      return FACE_IDLE;
    case PetState::Busy:      return FACE_BUSY;
    case PetState::Attention: return FACE_ATTENTION;
  }
  return FACE_IDLE;
}
