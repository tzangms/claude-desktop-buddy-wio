#include <unity.h>
#include "backlight.h"
#include "state.h"
#include "config.h"

void test_init_starts_awake() {
  backlightInit();
  TEST_ASSERT_TRUE(backlightIsAwake());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_starts_awake);
  return UNITY_END();
}
