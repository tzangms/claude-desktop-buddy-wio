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

void test_init_loads_existing_data() {
  _persistResetFakeFile();
  PersistData good{};
  good.magic = PERSIST_MAGIC;
  good.version = PERSIST_VERSION;
  good.appr = 17;
  good.deny = 3;
  good.lvl = 5;
  good.deviceLifetimeTokens = 250000;
  std::strcpy(good.deviceName, "Clawd");
  std::strcpy(good.ownerName, "Felix");
  persistMut() = good;
  persistCommit(true);
  persistInit();
  const PersistData& d = persistGet();
  TEST_ASSERT_EQUAL_INT32(17, d.appr);
  TEST_ASSERT_EQUAL_INT32(3, d.deny);
  TEST_ASSERT_EQUAL_INT32(5, d.lvl);
  TEST_ASSERT_EQUAL_INT64(250000, d.deviceLifetimeTokens);
  TEST_ASSERT_EQUAL_STRING("Clawd", d.deviceName);
  TEST_ASSERT_EQUAL_STRING("Felix", d.ownerName);
}

void test_init_rejects_wrong_magic() {
  _persistResetFakeFile();
  PersistData bad{};
  bad.magic = 0xDEADBEEF;
  bad.version = PERSIST_VERSION;
  bad.appr = 99;
  persistMut() = bad;
  persistCommit(true);
  persistInit();
  TEST_ASSERT_EQUAL_INT32(0, persistGet().appr);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, persistGet().magic);
}

void test_init_rejects_wrong_version() {
  _persistResetFakeFile();
  PersistData bad{};
  bad.magic = PERSIST_MAGIC;
  bad.version = 999;
  bad.appr = 99;
  persistMut() = bad;
  persistCommit(true);
  persistInit();
  TEST_ASSERT_EQUAL_INT32(0, persistGet().appr);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, persistGet().version);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_empty_uses_defaults);
  RUN_TEST(test_init_loads_existing_data);
  RUN_TEST(test_init_rejects_wrong_magic);
  RUN_TEST(test_init_rejects_wrong_version);
  return UNITY_END();
}
