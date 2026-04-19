#include <unity.h>
#include "character.h"
#include "manifest.h"

void test_pick_file_null_when_no_manifest() {
  _manifestResetForTest();
  _characterResetForTest();
  TEST_ASSERT_NULL(_characterPickFile(PetState::Idle, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pick_file_null_when_no_manifest);
  return UNITY_END();
}
