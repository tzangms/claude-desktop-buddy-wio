#include <unity.h>
#include <cstring>
#include "pet.h"
#include "state.h"

void test_sleep_when_advertising() {
  AppState s;
  s.mode = Mode::Advertising;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Sleep),
                    static_cast<int>(petComputeState(s)));
}

void test_sleep_when_disconnected() {
  AppState s;
  s.mode = Mode::Disconnected;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Sleep),
                    static_cast<int>(petComputeState(s)));
}

void test_idle_when_no_running() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.running = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Idle),
                    static_cast<int>(petComputeState(s)));
}

void test_busy_when_running_gt_zero() {
  AppState s;
  s.mode = Mode::Idle;
  s.hb.running = 2;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Busy),
                    static_cast<int>(petComputeState(s)));
}

void test_attention_when_prompt_mode() {
  AppState s;
  s.mode = Mode::Prompt;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Attention),
                    static_cast<int>(petComputeState(s)));
}

void test_ack_with_running_is_busy() {
  AppState s;
  s.mode = Mode::Ack;
  s.hb.running = 1;
  TEST_ASSERT_EQUAL(static_cast<int>(PetState::Busy),
                    static_cast<int>(petComputeState(s)));
}

void test_face_rows_non_null() {
  const PetState states[] = {
    PetState::Sleep, PetState::Idle, PetState::Busy, PetState::Attention,
  };
  for (PetState s : states) {
    const char* const* rows = petFace(s);
    TEST_ASSERT_NOT_NULL(rows);
    for (size_t i = 0; i < PET_FACE_LINES; ++i) {
      TEST_ASSERT_NOT_NULL(rows[i]);
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sleep_when_advertising);
  RUN_TEST(test_sleep_when_disconnected);
  RUN_TEST(test_idle_when_no_running);
  RUN_TEST(test_busy_when_running_gt_zero);
  RUN_TEST(test_attention_when_prompt_mode);
  RUN_TEST(test_ack_with_running_is_busy);
  RUN_TEST(test_face_rows_non_null);
  return UNITY_END();
}
