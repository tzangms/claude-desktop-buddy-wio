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

static const char* kBufoFull = R"({
  "name": "bufo",
  "colors": { "body":"#6B8E23","bg":"#000000","text":"#FFFFFF",
              "textDim":"#808080","ink":"#000000" },
  "states": {
    "sleep": "sleep.gif",
    "idle":  ["idle_0.gif","idle_1.gif","idle_2.gif"],
    "busy":      "busy.gif",
    "attention": "attention.gif",
    "celebrate": "celebrate.gif",
    "dizzy":     "dizzy.gif",
    "heart":     "heart.gif"
  }
})";

void test_parse_state_string_single_variant() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoFull, std::strlen(kBufoFull),
                                     m, err));
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_SLEEP]);
  TEST_ASSERT_EQUAL_STRING("sleep.gif", m.states[MANIFEST_STATE_SLEEP][0]);
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_BUSY]);
  TEST_ASSERT_EQUAL_STRING("busy.gif", m.states[MANIFEST_STATE_BUSY][0]);
}

void test_parse_state_missing_is_zero_count() {
  // kBufoFull has no "nap"
  CharManifest m{};
  std::string err;
  manifestParseJson(kBufoFull, std::strlen(kBufoFull), m, err);
  TEST_ASSERT_EQUAL_UINT8(0, m.stateVariantCount[MANIFEST_STATE_NAP]);
}

void test_parse_state_array_over_cap_truncates_with_warning() {
  // 20-element array
  std::string j = R"({"name":"x","colors":{"body":"#000000","bg":"#000000",
    "text":"#000000","textDim":"#000000","ink":"#000000"},
    "states":{"idle":[)";
  for (int i = 0; i < 20; ++i) {
    if (i) j += ",";
    j += "\"f" + std::to_string(i) + ".gif\"";
  }
  j += "]}}";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(j.c_str(), j.size(), m, err));
  TEST_ASSERT_EQUAL_UINT8(MANIFEST_MAX_VARIANTS,
                          m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_TRUE(err.find("truncated") != std::string::npos);
}

static const char* kBufoReal = R"({
  "name": "bufo",
  "colors": { "body":"#6B8E23","bg":"#000000","text":"#FFFFFF",
              "textDim":"#808080","ink":"#000000" },
  "states": {
    "sleep": "sleep.gif",
    "idle":  ["idle_0.gif","idle_1.gif","idle_2.gif","idle_3.gif",
              "idle_4.gif","idle_5.gif","idle_6.gif","idle_7.gif","idle_8.gif"],
    "busy":"busy.gif","attention":"attention.gif",
    "celebrate":"celebrate.gif","dizzy":"dizzy.gif","heart":"heart.gif"
  }
})";

void test_parse_unknown_state_ignored() {
  const char* j = R"({"name":"x","colors":{"body":"#000000","bg":"#000000",
    "text":"#000000","textDim":"#000000","ink":"#000000"},
    "states":{"dancing":"d.gif","idle":"i.gif"}})";
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(j, std::strlen(j), m, err));
  TEST_ASSERT_EQUAL_UINT8(1, m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_TRUE(err.empty());  // unknown is not an error
}

void test_parse_real_bufo_manifest() {
  CharManifest m{};
  std::string err;
  TEST_ASSERT_TRUE(manifestParseJson(kBufoReal, std::strlen(kBufoReal), m, err));
  TEST_ASSERT_EQUAL_STRING("bufo", m.name);
  TEST_ASSERT_EQUAL_UINT8(9, m.stateVariantCount[MANIFEST_STATE_IDLE]);
  TEST_ASSERT_EQUAL_STRING("idle_8.gif", m.states[MANIFEST_STATE_IDLE][8]);
  TEST_ASSERT_EQUAL_UINT8(0, m.stateVariantCount[MANIFEST_STATE_NAP]);
}

void test_active_set_and_get_roundtrip() {
  _manifestResetForTest();
  TEST_ASSERT_NULL(manifestActive());
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(kBufoReal,
                                              std::strlen(kBufoReal)));
  const CharManifest* m = manifestActive();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("bufo", m->name);
}

void test_active_failed_parse_leaves_prior_intact() {
  _manifestResetForTest();
  TEST_ASSERT_TRUE(_manifestSetActiveFromJson(kBufoReal,
                                              std::strlen(kBufoReal)));
  const char* bad = "{not json";
  TEST_ASSERT_FALSE(_manifestSetActiveFromJson(bad, std::strlen(bad)));
  const CharManifest* m = manifestActive();
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("bufo", m->name);  // prior stayed
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
  RUN_TEST(test_parse_state_string_single_variant);
  RUN_TEST(test_parse_state_missing_is_zero_count);
  RUN_TEST(test_parse_state_array_over_cap_truncates_with_warning);
  RUN_TEST(test_parse_unknown_state_ignored);
  RUN_TEST(test_parse_real_bufo_manifest);
  RUN_TEST(test_active_set_and_get_roundtrip);
  RUN_TEST(test_active_failed_parse_leaves_prior_intact);
  return UNITY_END();
}
