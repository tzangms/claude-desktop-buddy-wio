#include <unity.h>
#include "manifest.h"

void test_active_initially_null() {
  _manifestResetForTest();
  TEST_ASSERT_NULL(manifestActive());
}

void test_hex_to_rgb565_black() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565("#000000"));
}
void test_hex_to_rgb565_white() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, _manifestHex24ToRgb565("#FFFFFF"));
}
void test_hex_to_rgb565_bufo_body() {
  // #6B8E23 → R=0x6B (5 bits: 0x0D), G=0x8E (6 bits: 0x23), B=0x23 (5 bits: 0x04)
  // (0x0D<<11) | (0x23<<5) | 0x04 = 0x6C64.
  TEST_ASSERT_EQUAL_HEX16(0x6C64, _manifestHex24ToRgb565("#6B8E23"));
}
void test_hex_to_rgb565_bad_returns_zero() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565("notahex"));
  TEST_ASSERT_EQUAL_HEX16(0x0000, _manifestHex24ToRgb565(nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_active_initially_null);
  RUN_TEST(test_hex_to_rgb565_black);
  RUN_TEST(test_hex_to_rgb565_white);
  RUN_TEST(test_hex_to_rgb565_bufo_body);
  RUN_TEST(test_hex_to_rgb565_bad_returns_zero);
  return UNITY_END();
}
