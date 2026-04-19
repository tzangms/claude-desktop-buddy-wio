#include <unity.h>
#include "backlight.h"
#include "state.h"
#include "config.h"

void test_init_starts_awake() {
  backlightInit();
  TEST_ASSERT_TRUE(backlightIsAwake());
}

void test_tick_without_activity_sleeps_after_timeout() {
  backlightInit();
  AppState s;
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS - 1);
  TEST_ASSERT_TRUE(backlightIsAwake());
  backlightTick(s, BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
}

void test_wake_from_sleep_restores_awake() {
  backlightInit();
  AppState s;
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
  backlightWake(100000);
  TEST_ASSERT_TRUE(backlightIsAwake());
}

void test_prompt_mode_never_sleeps() {
  backlightInit();
  AppState s;
  s.mode = Mode::Prompt;
  backlightTick(s, BACKLIGHT_IDLE_MS * 10);
  TEST_ASSERT_TRUE(backlightIsAwake());
}

void test_prompt_mode_resets_timer_on_exit() {
  backlightInit();
  AppState s;
  s.mode = Mode::Prompt;
  backlightTick(s, BACKLIGHT_IDLE_MS * 2);     // still in Prompt, 2 min elapsed
  s.mode = Mode::Idle;
  backlightTick(s, BACKLIGHT_IDLE_MS * 2 + 1); // just exited, should NOT sleep immediately
  TEST_ASSERT_TRUE(backlightIsAwake());
  backlightTick(s, BACKLIGHT_IDLE_MS * 2 + BACKLIGHT_IDLE_MS + 1);
  TEST_ASSERT_FALSE(backlightIsAwake());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_starts_awake);
  RUN_TEST(test_tick_without_activity_sleeps_after_timeout);
  RUN_TEST(test_wake_from_sleep_restores_awake);
  RUN_TEST(test_prompt_mode_never_sleeps);
  RUN_TEST(test_prompt_mode_resets_timer_on_exit);
  return UNITY_END();
}
