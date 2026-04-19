#include "pet.h"
#include "state.h"
#include "config.h"

namespace {
  const char* const FRAMES_SLEEP[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " ,---.", " (- -)", " | z |", " '---'" },
    { " ,---.", " (- -)", " | . |", " '---'" },
    { " ,---.", " (- -)", " | z |", " '---'" },
  };
  const char* const FRAMES_IDLE[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " ,---.", " (o o)", " | _ |", " '---'" },
    { " ,---.", " (- -)", " | _ |", " '---'" },
    { " ,---.", " (o o)", " | _ |", " '---'" },
  };
  const char* const FRAMES_BUSY[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " ,---.", " (> <)", " | ~ |", " '---'" },
    { " ,---.", " (> <)", " | - |", " '---'" },
    { " ,---.", " (> <)", " | = |", " '---'" },
  };
  const char* const FRAMES_ATTENTION[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " ,---.", " (O O)", " | ! |", " '---'" },
    { " ,---.", " (O O)", " |!!!|", " '---'" },
    { " ,---.", " (O O)", " | ! |", " '---'" },
  };

  size_t frameIdx = 0;
  uint32_t lastFrameMs = 0;
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

const char* const* petFace(PetState state, size_t f) {
  if (f >= PET_FRAMES_PER_STATE) f = 0;
  switch (state) {
    case PetState::Sleep:     return FRAMES_SLEEP[f];
    case PetState::Idle:      return FRAMES_IDLE[f];
    case PetState::Busy:      return FRAMES_BUSY[f];
    case PetState::Attention: return FRAMES_ATTENTION[f];
  }
  return FRAMES_IDLE[0];
}

bool petTickFrame(uint32_t nowMs) {
  if ((nowMs - lastFrameMs) < PET_FRAME_MS) return false;
  frameIdx = (frameIdx + 1) % PET_FRAMES_PER_STATE;
  lastFrameMs = nowMs;
  return true;
}

size_t petCurrentFrame() {
  return frameIdx;
}

void petResetFrame(uint32_t nowMs) {
  frameIdx = 0;
  lastFrameMs = nowMs;
}
