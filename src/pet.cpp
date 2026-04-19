#include "pet.h"
#include "state.h"

namespace {
  const char* FACE_SLEEP     = " ,---.\n (- -)\n | z |\n '---'";
  const char* FACE_IDLE      = " ,---.\n (o o)\n | _ |\n '---'";
  const char* FACE_BUSY      = " ,---.\n (> <)\n | ~ |\n '---'";
  const char* FACE_ATTENTION = " ,---.\n (O O)\n | ! |\n '---'";
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

const char* petFace(PetState state) {
  switch (state) {
    case PetState::Sleep:     return FACE_SLEEP;
    case PetState::Idle:      return FACE_IDLE;
    case PetState::Busy:      return FACE_BUSY;
    case PetState::Attention: return FACE_ATTENTION;
  }
  return FACE_IDLE;
}
