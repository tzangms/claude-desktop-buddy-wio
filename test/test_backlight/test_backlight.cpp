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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_starts_awake);
  RUN_TEST(test_tick_without_activity_sleeps_after_timeout);
  return UNITY_END();
}
