#include <unity.h>
#include "state.h"
#include "config.h"

void test_apply_name_normal() {
  AppState s;
  std::string err;
  bool ok = applyNameCmd(s, "Clawd", err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Clawd", s.deviceName.c_str());
  TEST_ASSERT_TRUE(err.empty());
}

void test_apply_name_empty_rejected() {
  AppState s;
  s.deviceName = "OldName";
  std::string err;
  bool ok = applyNameCmd(s, "", err);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_STRING("OldName", s.deviceName.c_str());
  TEST_ASSERT_EQUAL_STRING("empty name", err.c_str());
}

void test_apply_name_truncates() {
  AppState s;
  std::string err;
  std::string longName(50, 'x');  // 50 chars, NAME_CHARS_MAX=32
  bool ok = applyNameCmd(s, longName, err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(NAME_CHARS_MAX, s.deviceName.size());
}

void test_apply_time_stores_values() {
  AppState s;
  applyTime(s, 1775731234, -25200, 9876);
  TEST_ASSERT_EQUAL_INT64(1775731234, s.timeEpoch);
  TEST_ASSERT_EQUAL_INT32(-25200, s.timeOffsetSec);
  TEST_ASSERT_EQUAL_UINT32(9876, s.timeSetAtMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_name_normal);
  RUN_TEST(test_apply_name_empty_rejected);
  RUN_TEST(test_apply_name_truncates);
  RUN_TEST(test_apply_time_stores_values);
  return UNITY_END();
}
