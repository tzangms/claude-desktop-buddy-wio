#include <unity.h>
#include "manifest.h"

void test_active_initially_null() {
  _manifestResetForTest();
  TEST_ASSERT_NULL(manifestActive());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_active_initially_null);
  return UNITY_END();
}
