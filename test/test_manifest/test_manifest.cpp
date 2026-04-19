#include <unity.h>
#include <cstring>
#include <string>
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

static const char* kBufoMinimal = R"({
  "name": "bufo",
  "colors": {
    "body": "#6B8E23", "bg": "#000000", "text": "#FFFFFF",
    "textDim": "#808080", "ink": "#000000"
  },
  "states": {}
})";

void test_parse_name_and_colors() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoMinimal, std::strlen(kBufoMinimal),
                                     m, err));
  TEST_ASSERT_EQUAL_STRING("bufo", m.name);
  TEST_ASSERT_EQUAL_HEX16(0x6C64, m.colorBody);
  TEST_ASSERT_EQUAL_HEX16(0x0000, m.colorBg);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, m.colorText);
  TEST_ASSERT_EQUAL_HEX16(0x8410, m.colorTextDim);  // #808080
  TEST_ASSERT_EQUAL_HEX16(0x0000, m.colorInk);
}

void test_parse_missing_name_rejects() {
  const char* j = R"({"colors":{"body":"#000000","bg":"#000000",
    "text":"#FFFFFF","textDim":"#808080","ink":"#000000"},"states":{}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_TRUE(err.find("name") != std::string::npos);
}

void test_parse_missing_colors_rejects() {
  const char* j = R"({"name":"bufo","states":{}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_TRUE(err.find("colors") != std::string::npos);
}

void test_parse_malformed_json_rejects() {
  const char* j = "{not json";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_FALSE(manifestParseJson(j, std::strlen(j), m, err));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_active_initially_null);
  RUN_TEST(test_hex_to_rgb565_black);
  RUN_TEST(test_hex_to_rgb565_white);
  RUN_TEST(test_hex_to_rgb565_bufo_body);
  RUN_TEST(test_hex_to_rgb565_bad_returns_zero);
  RUN_TEST(test_parse_name_and_colors);
  RUN_TEST(test_parse_missing_name_rejects);
  RUN_TEST(test_parse_missing_colors_rejects);
  RUN_TEST(test_parse_malformed_json_rejects);
  return UNITY_END();
}
