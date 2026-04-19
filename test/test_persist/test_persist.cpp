#include <unity.h>
#include <cstring>
#include "persist.h"
#include "config.h"

void test_init_empty_uses_defaults() {
  _persistResetFakeFile();
  persistInit();
  const PersistData& d = persistGet();
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, d.magic);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, d.version);
  TEST_ASSERT_EQUAL_INT32(0, d.appr);
  TEST_ASSERT_EQUAL_INT32(0, d.deny);
  TEST_ASSERT_EQUAL_INT32(0, d.lvl);
  TEST_ASSERT_EQUAL_STRING("", d.deviceName);
  TEST_ASSERT_EQUAL_STRING("", d.ownerName);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_empty_uses_defaults);
  return UNITY_END();
}
