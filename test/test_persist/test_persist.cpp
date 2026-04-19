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
  persistTick(0);
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
  persistTick(0);
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
  persistTick(0);
  persistInit();
  TEST_ASSERT_EQUAL_INT32(0, persistGet().appr);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, persistGet().version);
}

void test_commit_immediate_flushes_on_next_tick() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 7;
  persistCommit(true);
  // Deferred: no flush yet.
  TEST_ASSERT_EQUAL(before, _persistWriteCount());
  persistTick(0);
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
  persistInit();
  TEST_ASSERT_EQUAL_INT32(7, persistGet().appr);
}

void test_commit_debounced_waits_under_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 1;
  persistCommit(false);
  persistTick(PERSIST_DEBOUNCE_MS - 1);
  TEST_ASSERT_EQUAL(before, _persistWriteCount());
}

void test_tick_flushes_after_time_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().appr = 2;
  persistCommit(false);
  persistTick(PERSIST_DEBOUNCE_MS + 1);
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
}

void test_tick_skips_when_clean() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistTick(PERSIST_DEBOUNCE_MS * 10);
  TEST_ASSERT_EQUAL(before, _persistWriteCount());
}

void test_tick_flushes_after_token_delta_threshold() {
  _persistResetFakeFile();
  persistInit();
  int before = _persistWriteCount();
  persistMut().deviceLifetimeTokens = PERSIST_DEBOUNCE_TOKENS + 1;
  persistCommit(false);
  persistTick(1000);
  TEST_ASSERT_EQUAL(before + 1, _persistWriteCount());
}

void test_heartbeat_first_call_sets_baseline_only() {
  _persistResetFakeFile();
  persistInit();
  // First heartbeat after boot establishes the baseline; lifetime stays 0
  // so we don't double-count tokens already folded in before shutdown.
  persistUpdateFromHeartbeat(15000, 1234);
  TEST_ASSERT_EQUAL_INT64(0, persistGet().deviceLifetimeTokens);
  TEST_ASSERT_EQUAL_INT64(1234, persistGet().tokens_today);
  TEST_ASSERT_EQUAL_INT32(0, persistGet().lvl);
}

void test_heartbeat_level_increments_every_50k_tokens() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(0, 0);       // baseline
  persistUpdateFromHeartbeat(50000, 0);   // +50000 → lvl 1
  TEST_ASSERT_EQUAL_INT32(1, persistGet().lvl);
  persistUpdateFromHeartbeat(120000, 0);  // +70000 → lifetime 120000 → lvl 2
  TEST_ASSERT_EQUAL_INT32(2, persistGet().lvl);
}

void test_heartbeat_desktop_restart_no_negative_delta() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(0, 0);       // baseline
  persistUpdateFromHeartbeat(10000, 0);   // lifetime 10000
  int64_t lifetimeBefore = persistGet().deviceLifetimeTokens;
  persistUpdateFromHeartbeat(500, 0);     // desktop restart: ignored, resyncs baseline
  TEST_ASSERT_EQUAL_INT64(lifetimeBefore, persistGet().deviceLifetimeTokens);
  persistUpdateFromHeartbeat(2500, 0);    // delta 2000
  TEST_ASSERT_EQUAL_INT64(lifetimeBefore + 2000, persistGet().deviceLifetimeTokens);
}

void test_heartbeat_updates_tokens_today() {
  _persistResetFakeFile();
  persistInit();
  persistUpdateFromHeartbeat(0, 100);
  TEST_ASSERT_EQUAL_INT64(100, persistGet().tokens_today);
  persistUpdateFromHeartbeat(0, 5000);
  TEST_ASSERT_EQUAL_INT64(5000, persistGet().tokens_today);
  persistUpdateFromHeartbeat(0, 0);
  TEST_ASSERT_EQUAL_INT64(0, persistGet().tokens_today);
}

void test_active_char_roundtrip() {
  _persistResetFakeFile();
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());

  persistSetActiveChar("bufo");
  persistTick(0);   // force flush of forceFlushNextTick
  persistTick(PERSIST_DEBOUNCE_MS + 1);

  // Re-init simulates a reboot.
  persistInit();
  TEST_ASSERT_EQUAL_STRING("bufo", persistGetActiveChar());
}

void test_active_char_default_empty_on_fresh_init() {
  _persistResetFakeFile();
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());
}

void test_v1_sized_blob_falls_to_defaults() {
  // Simulate a stored blob from before activeCharName was added.
  // The size-mismatch guard in readStore must reject it so setDefaults
  // runs and we don't read garbage out of the new field.
  _persistResetFakeFile();
  uint8_t fake[32] = {0};   // small, clearly not sizeof(PersistData)
  _persistSeedFakeFile(fake, sizeof(fake));
  persistInit();
  TEST_ASSERT_EQUAL_STRING("", persistGetActiveChar());
  TEST_ASSERT_EQUAL_UINT32(PERSIST_MAGIC, persistGet().magic);
  TEST_ASSERT_EQUAL_UINT32(PERSIST_VERSION, persistGet().version);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_init_empty_uses_defaults);
  RUN_TEST(test_init_loads_existing_data);
  RUN_TEST(test_init_rejects_wrong_magic);
  RUN_TEST(test_init_rejects_wrong_version);
  RUN_TEST(test_commit_immediate_flushes_on_next_tick);
  RUN_TEST(test_commit_debounced_waits_under_threshold);
  RUN_TEST(test_tick_flushes_after_time_threshold);
  RUN_TEST(test_tick_skips_when_clean);
  RUN_TEST(test_tick_flushes_after_token_delta_threshold);
  RUN_TEST(test_heartbeat_first_call_sets_baseline_only);
  RUN_TEST(test_heartbeat_level_increments_every_50k_tokens);
  RUN_TEST(test_heartbeat_desktop_restart_no_negative_delta);
  RUN_TEST(test_heartbeat_updates_tokens_today);
  RUN_TEST(test_active_char_roundtrip);
  RUN_TEST(test_active_char_default_empty_on_fresh_init);
  RUN_TEST(test_v1_sized_blob_falls_to_defaults);
  return UNITY_END();
}
