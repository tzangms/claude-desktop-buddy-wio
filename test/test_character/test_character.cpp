#include <unity.h>
#include "character.h"
#include "manifest.h"
#include "config.h"

#include <cstring>
#include <string>

// Manifest with all 7 host-side states (no nap), 3-variant idle. Matches
// the real bufo shape but compressed for readability.
static const char* kManifestIdleBusySleepFull = R"({
  "name":"bufo",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{
    "sleep":"sleep.gif",
    "idle":["idle_0.gif","idle_1.gif","idle_2.gif"],
    "busy":"busy.gif",
    "attention":"attention.gif",
    "celebrate":"celebrate.gif",
    "dizzy":"dizzy.gif",
    "heart":"heart.gif"
  }
})";

// Manifest with ONLY sleep — everything else should fall back.
static const char* kManifestSleepOnly = R"({
  "name":"minimal",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{"sleep":"only_sleep.gif"}
})";

// Manifest with ONLY idle — sleep fallback should fall through to idle[0].
static const char* kManifestIdleOnly = R"({
  "name":"idleonly",
  "colors":{"body":"#000000","bg":"#000000","text":"#FFFFFF","textDim":"#808080","ink":"#000000"},
  "states":{"idle":"just_idle.gif"}
})";

static void setActive(const char* json) {
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(json, std::strlen(json)));
}

void test_pick_file_null_when_no_manifest() {
  _manifestResetForTest();
  _characterResetForTest();
  TEST_ASSERT_NULL(_characterPickFile(PetState::Idle, 0));
}

void test_pick_file_idle_first_variant() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif", _characterPickFile(PetState::Idle, 0));
}

void test_pick_file_idle_does_not_advance_before_dwell() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  _characterPickFile(PetState::Idle, 0);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, VARIANT_DWELL_MS - 1));
}

void test_pick_file_idle_advances_after_dwell() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  _characterPickFile(PetState::Idle, 0);
  TEST_ASSERT_EQUAL_STRING("idle_1.gif",
                           _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1));
}

void test_pick_file_idle_wraps_to_zero() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // 3 variants: 0 → 1 → 2 → 0.
  _characterPickFile(PetState::Idle, 0);
  _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1);        // 1
  _characterPickFile(PetState::Idle, 2 * VARIANT_DWELL_MS + 2);    // 2
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, 3 * VARIANT_DWELL_MS + 3));
}

void test_pick_file_non_idle_state_does_not_rotate() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // Repeated Busy calls return the same file regardless of elapsed time.
  TEST_ASSERT_EQUAL_STRING("busy.gif", _characterPickFile(PetState::Busy, 0));
  TEST_ASSERT_EQUAL_STRING("busy.gif",
                           _characterPickFile(PetState::Busy, 10 * VARIANT_DWELL_MS));
}

void test_pick_file_state_change_resets_variant() {
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  // Advance Idle to variant 2.
  _characterPickFile(PetState::Idle, 0);
  _characterPickFile(PetState::Idle, VARIANT_DWELL_MS + 1);
  _characterPickFile(PetState::Idle, 2 * VARIANT_DWELL_MS + 2);
  // Switch to Busy then back to Idle — Idle variant should restart from 0.
  _characterPickFile(PetState::Busy, 3 * VARIANT_DWELL_MS);
  TEST_ASSERT_EQUAL_STRING("idle_0.gif",
                           _characterPickFile(PetState::Idle, 4 * VARIANT_DWELL_MS));
}

void test_pick_file_missing_state_falls_back_to_sleep() {
  // PetState::Nap is not in the manifest; spec says fall back to sleep.
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleBusySleepFull);
  TEST_ASSERT_EQUAL_STRING("sleep.gif", _characterPickFile(PetState::Nap, 0));
}

void test_pick_file_fallback_chain_sleep_then_idle() {
  // Manifest has only idle; asking for Busy should fall: busy(missing)
  // → sleep(missing) → idle[0].
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestIdleOnly);
  TEST_ASSERT_EQUAL_STRING("just_idle.gif",
                           _characterPickFile(PetState::Busy, 0));
}

void test_pick_file_empty_manifest_returns_null() {
  // Manifest with only sleep: asking for Nap falls to sleep OK.
  _manifestResetForTest(); _characterResetForTest();
  setActive(kManifestSleepOnly);
  TEST_ASSERT_EQUAL_STRING("only_sleep.gif",
                           _characterPickFile(PetState::Nap, 0));
  // But asking with a manifest that has NEITHER sleep NOR idle should be null.
  // Simulate by building such a manifest inline.
  const char* j = R"({
    "name":"busy_only",
    "colors":{"body":"#000000","bg":"#000000","text":"#000000","textDim":"#000000","ink":"#000000"},
    "states":{"busy":"b.gif"}
  })";
  _manifestResetForTest(); _characterResetForTest();
  setActive(j);
  TEST_ASSERT_NULL(_characterPickFile(PetState::Idle, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pick_file_null_when_no_manifest);
  RUN_TEST(test_pick_file_idle_first_variant);
  RUN_TEST(test_pick_file_idle_does_not_advance_before_dwell);
  RUN_TEST(test_pick_file_idle_advances_after_dwell);
  RUN_TEST(test_pick_file_idle_wraps_to_zero);
  RUN_TEST(test_pick_file_non_idle_state_does_not_rotate);
  RUN_TEST(test_pick_file_state_change_resets_variant);
  RUN_TEST(test_pick_file_missing_state_falls_back_to_sleep);
  RUN_TEST(test_pick_file_fallback_chain_sleep_then_idle);
  RUN_TEST(test_pick_file_empty_manifest_returns_null);
  return UNITY_END();
}
