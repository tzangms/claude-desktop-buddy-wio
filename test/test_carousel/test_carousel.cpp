#include <unity.h>
#include <cstring>
#include "carousel.h"
#include "state.h"
#include "persist.h"
#include "config.h"

void test_enumerate_empty_list() {
  _carouselSetFakeChars(nullptr, 0);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_enumerate_sorts_alphabetically() {
  const char* input[] = {"zebra", "apple", "mango"};
  _carouselSetFakeChars(input, 3);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(3, n);
  TEST_ASSERT_EQUAL_STRING("apple", out[0]);
  TEST_ASSERT_EQUAL_STRING("mango", out[1]);
  TEST_ASSERT_EQUAL_STRING("zebra", out[2]);
}

void test_enumerate_truncates_at_max() {
  const char* input[20] = {
    "a01","a02","a03","a04","a05","a06","a07","a08",
    "a09","a10","a11","a12","a13","a14","a15","a16",
    "a17","a18","a19","a20",
  };
  _carouselSetFakeChars(input, 20);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, CAROUSEL_MAX_CHARS);
  TEST_ASSERT_EQUAL_UINT(CAROUSEL_MAX_CHARS, n);
  // First 16 in sorted order should survive; a17..a20 dropped.
  TEST_ASSERT_EQUAL_STRING("a01", out[0]);
  TEST_ASSERT_EQUAL_STRING("a16", out[15]);
}

void test_enumerate_respects_caller_max() {
  const char* input[] = {"charlie", "alpha", "bravo", "delta", "echo"};
  _carouselSetFakeChars(input, 5);
  CarouselName out[CAROUSEL_MAX_CHARS];
  size_t n = carouselEnumerate(out, 2);
  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_STRING("alpha", out[0]);
  TEST_ASSERT_EQUAL_STRING("bravo", out[1]);
}

static void resetCarouselState() {
  _persistResetFakeFile();
  persistInit();
  _carouselSetFakeChars(nullptr, 0);
}

void test_advance_zero_chars_is_noop() {
  resetCarouselState();
  AppState s;
  s.buddyOverlayUntilMs = 0;
  bool ok = carouselAdvance(s, true, 1000);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0, s.buddyOverlayUntilMs);
  TEST_ASSERT_EQUAL_STRING("", s.buddyOverlayName);
}

void test_advance_one_char_sets_overlay_only() {
  resetCarouselState();
  persistSetActiveChar("bufo");
  const char* input[] = {"bufo"};
  _carouselSetFakeChars(input, 1);

  AppState s;
  bool ok = carouselAdvance(s, true, 5000);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(5000 + BUDDY_OVERLAY_MS, s.buddyOverlayUntilMs);
  TEST_ASSERT_EQUAL_STRING("bufo", s.buddyOverlayName);
  TEST_ASSERT_EQUAL_STRING("bufo", persistGetActiveChar());
}

void test_advance_forward_wraps() {
  resetCarouselState();
  persistSetActiveChar("alpha");
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  carouselAdvance(s, true, 100);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
  TEST_ASSERT_EQUAL_STRING("beta", s.buddyOverlayName);

  carouselAdvance(s, true, 200);
  TEST_ASSERT_EQUAL_STRING("gamma", persistGetActiveChar());

  carouselAdvance(s, true, 300);
  TEST_ASSERT_EQUAL_STRING("alpha", persistGetActiveChar());
}

void test_advance_backward_wraps() {
  resetCarouselState();
  persistSetActiveChar("alpha");
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  carouselAdvance(s, false, 100);
  TEST_ASSERT_EQUAL_STRING("gamma", persistGetActiveChar());

  carouselAdvance(s, false, 200);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
}

void test_advance_when_active_not_in_list() {
  resetCarouselState();
  persistSetActiveChar("deleted-char");
  const char* input[] = {"alpha", "beta", "gamma"};
  _carouselSetFakeChars(input, 3);

  AppState s;
  carouselAdvance(s, true, 100);
  TEST_ASSERT_EQUAL_STRING("beta", persistGetActiveChar());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_enumerate_empty_list);
  RUN_TEST(test_enumerate_sorts_alphabetically);
  RUN_TEST(test_enumerate_truncates_at_max);
  RUN_TEST(test_enumerate_respects_caller_max);
  RUN_TEST(test_advance_zero_chars_is_noop);
  RUN_TEST(test_advance_one_char_sets_overlay_only);
  RUN_TEST(test_advance_forward_wraps);
  RUN_TEST(test_advance_backward_wraps);
  RUN_TEST(test_advance_when_active_not_in_list);
  return UNITY_END();
}
