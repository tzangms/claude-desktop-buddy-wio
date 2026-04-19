#include <unity.h>
#include <cstring>
#include "pet.h"
#include "state.h"
#include "config.h"

// Helper: force both overrides to expire so petComputeState returns
// mode-derived state. Calling petTriggerCelebrate/Heart at 0 sets
// celebrateUntilMs = PET_CELEBRATE_MS; calling petComputeState at a
// sufficiently large nowMs returns the mode-derived state again.
static constexpr uint32_t FAR_FUTURE = 1000000;

void test_sleep_when_advertising() {
  AppState s;
  s.mode = Mode::Advertising;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Sleep),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_sleep_when_disconnected() {
  AppState s;
  s.mode = Mode::Disconnected;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Sleep),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_idle_when_no_running() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.running = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Idle),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_busy_when_running_gt_zero() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.running = 2;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Busy),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_attention_when_prompt_mode() {
  AppState s;
  s.mode = Mode::Prompt;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Attention),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_ack_with_running_is_busy() {
  AppState s;
  s.mode = Mode::Ack;
  s.hb.running = 1;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Busy),
                    static_cast<int>(petComputeState(s, FAR_FUTURE)));
}

void test_face_rows_non_null_all_frames() {
  const PetState states[] = {
    PetState::Sleep, PetState::Idle, PetState::Busy,
    PetState::Attention, PetState::Celebrate, PetState::Heart,
  };
  for (PetState s : states) {
    for (size_t f = 0; f < PET_FRAMES_PER_STATE; ++f) {
      const char* const* rows = petFace(s, f);
      TEST_ASSERT_NOT_NULL(rows);
      for (size_t i = 0; i < PET_FACE_LINES; ++i) {
        TEST_ASSERT_NOT_NULL(rows[i]);
      }
    }
  }
}

void test_tick_no_advance_within_window() {
  petResetFrame(10000);
  size_t before = petCurrentFrame();
  bool advanced = petTickFrame(10000 + PET_FRAME_MS - 1);
  TEST_ASSERT_FALSE(advanced);
  TEST_ASSERT_EQUAL(before, petCurrentFrame());
}

void test_tick_advances_at_window() {
  petResetFrame(20000);
  size_t before = petCurrentFrame();
  bool advanced = petTickFrame(20000 + PET_FRAME_MS);
  TEST_ASSERT_TRUE(advanced);
  TEST_ASSERT_EQUAL((before + 1) % PET_FRAMES_PER_STATE, petCurrentFrame());
}

void test_tick_wraps() {
  petResetFrame(30000);
  for (size_t i = 0; i < PET_FRAMES_PER_STATE; ++i) {
    petTickFrame(30000 + (i + 1) * PET_FRAME_MS);
  }
  TEST_ASSERT_EQUAL(0, petCurrentFrame());
}

void test_reset_snaps_to_zero() {
  petResetFrame(40000);
  petTickFrame(40000 + PET_FRAME_MS);
  TEST_ASSERT_NOT_EQUAL(0, petCurrentFrame());
  petResetFrame(50000);
  TEST_ASSERT_EQUAL(0, petCurrentFrame());
}

void test_celebrate_overrides_idle() {
  AppState s;
  s.mode = Mode::Idle;
  uint32_t now = 100000;
  petTriggerCelebrate(now);
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Celebrate),
                    static_cast<int>(petComputeState(s, now + 100)));
  // After expiry, mode-derived state returns.
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Idle),
                    static_cast<int>(petComputeState(s, now + PET_CELEBRATE_MS + 1)));
}

void test_heart_overrides_idle() {
  AppState s;
  s.mode = Mode::Idle;
  uint32_t now = 200000;
  petTriggerHeart(now);
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Heart),
                    static_cast<int>(petComputeState(s, now + 100)));
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Idle),
                    static_cast<int>(petComputeState(s, now + PET_HEART_MS + 1)));
}

void test_celebrate_outranks_heart() {
  AppState s;
  s.mode = Mode::Idle;
  uint32_t now = 300000;
  petTriggerHeart(now);
  petTriggerCelebrate(now);
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Celebrate),
                    static_cast<int>(petComputeState(s, now + 100)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sleep_when_advertising);
  RUN_TEST(test_sleep_when_disconnected);
  RUN_TEST(test_idle_when_no_running);
  RUN_TEST(test_busy_when_running_gt_zero);
  RUN_TEST(test_attention_when_prompt_mode);
  RUN_TEST(test_ack_with_running_is_busy);
  RUN_TEST(test_face_rows_non_null_all_frames);
  RUN_TEST(test_tick_no_advance_within_window);
  RUN_TEST(test_tick_advances_at_window);
  RUN_TEST(test_tick_wraps);
  RUN_TEST(test_reset_snaps_to_zero);
  RUN_TEST(test_celebrate_overrides_idle);
  RUN_TEST(test_heart_overrides_idle);
  RUN_TEST(test_celebrate_outranks_heart);
  return UNITY_END();
}
