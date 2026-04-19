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
  const char* const FRAMES_CELEBRATE[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " \\o/ ", " (^o^)", " | * |", " '---'" },
    { " _o_ ", " (^o^)", " |   |", " '---'" },
    { " \\o/ ", " (^o^)", " | * |", " '---'" },
  };
  const char* const FRAMES_HEART[PET_FRAMES_PER_STATE][PET_FACE_LINES] = {
    { " <3 <3", " (^_^)", " | v |", " '---'" },
    { "  <3  ", " (^_^)", " | v |", " '---'" },
    { " <3 <3", " (^_^)", " | v |", " '---'" },
  };

  size_t frameIdx = 0;
  uint32_t lastFrameMs = 0;
  uint32_t celebrateUntilMs = 0;
  uint32_t heartUntilMs = 0;

  inline bool active(uint32_t untilMs, uint32_t nowMs) {
    // Signed diff handles uint32 wrap.
    return static_cast<int32_t>(untilMs - nowMs) > 0;
  }
}

PetState petComputeState(const AppState& s, uint32_t nowMs) {
  if (active(celebrateUntilMs, nowMs)) return PetState::Celebrate;
  if (active(heartUntilMs, nowMs))     return PetState::Heart;
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
    case PetState::Celebrate: return FRAMES_CELEBRATE[f];
    case PetState::Heart:     return FRAMES_HEART[f];
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

void petTriggerCelebrate(uint32_t nowMs) {
  celebrateUntilMs = nowMs + PET_CELEBRATE_MS;
}

void petTriggerHeart(uint32_t nowMs) {
  heartUntilMs = nowMs + PET_HEART_MS;
}
