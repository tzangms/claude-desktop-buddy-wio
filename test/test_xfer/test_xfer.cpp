#include <unity.h>
#include <cstring>
#include "xfer.h"
#include "config.h"

// --- name / path validation ---

void test_valid_names() {
  TEST_ASSERT_TRUE(xferIsValidName("bufo"));
  TEST_ASSERT_TRUE(xferIsValidName("manifest.json"));
  TEST_ASSERT_TRUE(xferIsValidName("idle_0.gif"));
  TEST_ASSERT_TRUE(xferIsValidName("a"));
  TEST_ASSERT_TRUE(xferIsValidName("my-pet.gif"));
}

void test_rejects_empty() {
  TEST_ASSERT_FALSE(xferIsValidName(""));
  TEST_ASSERT_FALSE(xferIsValidName(nullptr));
}

void test_rejects_dotfile() {
  TEST_ASSERT_FALSE(xferIsValidName(".hidden"));
  TEST_ASSERT_FALSE(xferIsValidName(".DS_Store"));
}

void test_rejects_double_dot() {
  TEST_ASSERT_FALSE(xferIsValidName(".."));
  TEST_ASSERT_FALSE(xferIsValidName("../etc/passwd"));
  TEST_ASSERT_FALSE(xferIsValidName("a..b"));
}

void test_rejects_slashes() {
  TEST_ASSERT_FALSE(xferIsValidName("/absolute/path"));
  TEST_ASSERT_FALSE(xferIsValidName("sub/dir/file"));
  TEST_ASSERT_FALSE(xferIsValidName("back\\slash"));
}

void test_rejects_too_long() {
  char long64[65];
  std::memset(long64, 'x', 64);
  long64[64] = '\0';
  TEST_ASSERT_FALSE(xferIsValidName(long64));
}

// --- base64 ---

void test_base64_basic() {
  // "hello" (5 bytes) → "aGVsbG8=" (8 chars)
  uint8_t out[8] = {0};
  int64_t n = xferBase64Decode("aGVsbG8=", 8, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT64(5, n);
  TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
}

void test_base64_no_padding() {
  // "hel" (3 bytes) → "aGVs" (4 chars, no pad)
  uint8_t out[4] = {0};
  int64_t n = xferBase64Decode("aGVs", 4, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT64(3, n);
  TEST_ASSERT_EQUAL_MEMORY("hel", out, 3);
}

void test_base64_two_pad() {
  // "h" (1 byte) → "aA==" (4 chars, two pad)
  uint8_t out[2] = {0};
  int64_t n = xferBase64Decode("aA==", 4, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT64(1, n);
  TEST_ASSERT_EQUAL_UINT8('h', out[0]);
}

void test_base64_bad_char() {
  uint8_t out[4] = {0};
  TEST_ASSERT_EQUAL_INT64(-1, xferBase64Decode("aGV!", 4, out, sizeof(out)));
}

void test_base64_bad_length() {
  uint8_t out[4] = {0};
  TEST_ASSERT_EQUAL_INT64(-1, xferBase64Decode("aGV", 3, out, sizeof(out)));
}

void test_base64_output_overflow() {
  uint8_t out[1] = {0};
  TEST_ASSERT_EQUAL_INT64(-1, xferBase64Decode("aGVsbG8=", 8, out, sizeof(out)));
}

// --- state machine ---

void test_begin_char_requires_idle() {
  _xferResetForTest();
  TEST_ASSERT_TRUE(xferBeginChar("bufo", 1000));
  TEST_ASSERT_EQUAL(1, _xferStateOrdinal());  // CharOpen
  TEST_ASSERT_FALSE(xferBeginChar("bufo2", 500));  // can't re-begin
}

void test_begin_char_rejects_bad_name() {
  _xferResetForTest();
  TEST_ASSERT_FALSE(xferBeginChar("../evil", 1000));
  TEST_ASSERT_FALSE(xferBeginChar("", 1000));
}

void test_begin_char_rejects_over_budget() {
  _xferResetForTest();
  TEST_ASSERT_FALSE(xferBeginChar("bufo", (int64_t)CHAR_MAX_TOTAL_BYTES + 1));
}

void test_file_requires_char_open() {
  _xferResetForTest();
  TEST_ASSERT_FALSE(xferBeginFile("manifest.json", 100));
}

void test_chunk_requires_file_open() {
  _xferResetForTest();
  int64_t n = 0;
  TEST_ASSERT_FALSE(xferChunk("aGVsbG8=", n));
  xferBeginChar("bufo", 1000);
  TEST_ASSERT_FALSE(xferChunk("aGVsbG8=", n));
}

void test_full_file_roundtrip() {
  _xferResetForTest();
  TEST_ASSERT_TRUE(xferBeginChar("bufo", 1000));
  TEST_ASSERT_TRUE(xferBeginFile("manifest.json", 5));
  TEST_ASSERT_EQUAL(2, _xferStateOrdinal());  // FileOpen
  int64_t n = 0;
  TEST_ASSERT_TRUE(xferChunk("aGVsbG8=", n));  // "hello"
  TEST_ASSERT_EQUAL_INT64(5, n);
  int64_t finalSize = 0;
  TEST_ASSERT_TRUE(xferEndFile(finalSize));
  TEST_ASSERT_EQUAL_INT64(5, finalSize);
  TEST_ASSERT_EQUAL(1, _xferStateOrdinal());  // back to CharOpen
  TEST_ASSERT_EQUAL(5u, _xferLastFileSize());
  TEST_ASSERT_EQUAL_MEMORY("hello", _xferLastFileBytes(), 5);
  TEST_ASSERT_TRUE(xferEndChar());
  TEST_ASSERT_EQUAL(0, _xferStateOrdinal());  // Idle
}

void test_multiple_chunks_concat() {
  _xferResetForTest();
  xferBeginChar("bufo", 1000);
  xferBeginFile("data.bin", 11);
  int64_t n = 0;
  TEST_ASSERT_TRUE(xferChunk("aGVs", n));     // "hel"
  TEST_ASSERT_EQUAL_INT64(3, n);
  TEST_ASSERT_TRUE(xferChunk("bG8g", n));     // "lo "
  TEST_ASSERT_EQUAL_INT64(6, n);
  TEST_ASSERT_TRUE(xferChunk("d29ybGQ=", n)); // "world"
  TEST_ASSERT_EQUAL_INT64(11, n);
  int64_t finalSize = 0;
  TEST_ASSERT_TRUE(xferEndFile(finalSize));
  TEST_ASSERT_EQUAL_INT64(11, finalSize);
  TEST_ASSERT_EQUAL_MEMORY("hello world", _xferLastFileBytes(), 11);
}

void test_end_file_fails_on_size_mismatch() {
  _xferResetForTest();
  xferBeginChar("bufo", 1000);
  xferBeginFile("short.bin", 10);  // declared 10
  int64_t n = 0;
  xferChunk("aGVsbG8=", n);         // only 5 bytes written
  int64_t finalSize = 0;
  TEST_ASSERT_FALSE(xferEndFile(finalSize));  // short-write rejected
  TEST_ASSERT_EQUAL_INT64(5, finalSize);
}

void test_bad_chunk_base64_fails() {
  _xferResetForTest();
  xferBeginChar("bufo", 1000);
  xferBeginFile("data.bin", 10);
  int64_t n = 0;
  TEST_ASSERT_FALSE(xferChunk("not!base64!", n));
}

void test_active_char_name() {
  _xferResetForTest();
  TEST_ASSERT_EQUAL_STRING("", xferActiveCharName());
  xferBeginChar("bufo", 100);
  TEST_ASSERT_EQUAL_STRING("bufo", xferActiveCharName());
  xferEndChar();
  TEST_ASSERT_EQUAL_STRING("bufo", xferActiveCharName());  // latched
}

// --- deferred queue / tick (callback-safe path) ---

void test_tick_empty_returns_none() {
  _xferResetForTest();
  XferAckInfo a = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::None, a.kind);
}

void test_queue_char_begin_deferred_until_tick() {
  _xferResetForTest();
  TEST_ASSERT_TRUE(xferQueueCharBegin("bufo", 1000));
  // SFUD / state work MUST NOT have happened yet.
  TEST_ASSERT_EQUAL(0, _xferStateOrdinal());  // still Idle
  TEST_ASSERT_TRUE(xferHasPending());

  XferAckInfo a = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::CharBegin, a.kind);
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_EQUAL(1, _xferStateOrdinal());  // CharOpen after tick
  TEST_ASSERT_FALSE(xferHasPending());
}

void test_queue_while_pending_rejected() {
  _xferResetForTest();
  TEST_ASSERT_TRUE(xferQueueCharBegin("bufo", 1000));
  TEST_ASSERT_FALSE(xferQueueCharBegin("other", 500));  // still pending
}

void test_queue_chunk_not_written_until_tick() {
  _xferResetForTest();
  xferQueueCharBegin("bufo", 1000);
  xferTick();
  xferQueueFileBegin("x.bin", 5);
  xferTick();

  TEST_ASSERT_TRUE(xferQueueChunk("aGVsbG8="));  // "hello"
  // Bytes not written to backing store yet.
  TEST_ASSERT_EQUAL(0u, _xferLastFileSize());

  XferAckInfo a = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::Chunk, a.kind);
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_EQUAL_INT64(5, a.n);
}

void test_full_roundtrip_via_queue_tick() {
  _xferResetForTest();

  TEST_ASSERT_TRUE(xferQueueCharBegin("bufo", 1000));
  XferAckInfo a1 = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::CharBegin, a1.kind);
  TEST_ASSERT_TRUE(a1.ok);

  TEST_ASSERT_TRUE(xferQueueFileBegin("data.bin", 11));
  XferAckInfo a2 = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::FileBegin, a2.kind);
  TEST_ASSERT_TRUE(a2.ok);

  TEST_ASSERT_TRUE(xferQueueChunk("aGVsbG8g"));     // "hello "
  XferAckInfo a3 = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::Chunk, a3.kind);
  TEST_ASSERT_EQUAL_INT64(6, a3.n);

  TEST_ASSERT_TRUE(xferQueueChunk("d29ybGQ="));     // "world"
  xferTick();

  TEST_ASSERT_TRUE(xferQueueFileEnd());
  XferAckInfo a4 = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::FileEnd, a4.kind);
  TEST_ASSERT_TRUE(a4.ok);
  TEST_ASSERT_EQUAL_INT64(11, a4.n);
  TEST_ASSERT_EQUAL_MEMORY("hello world", _xferLastFileBytes(), 11);

  TEST_ASSERT_TRUE(xferQueueCharEnd());
  XferAckInfo a5 = xferTick();
  TEST_ASSERT_EQUAL(XferAckInfo::CharEnd, a5.kind);
  TEST_ASSERT_TRUE(a5.ok);
  TEST_ASSERT_EQUAL(0, _xferStateOrdinal());
}

void test_queue_file_begin_chunk_end_acks_carry_n() {
  // file_end ack should include the actual byte count so host can verify.
  _xferResetForTest();
  xferQueueCharBegin("bufo", 1000); xferTick();
  xferQueueFileBegin("a.bin", 3);   xferTick();
  xferQueueChunk("aGVs");           // "hel" = 3 bytes
  XferAckInfo a = xferTick();
  TEST_ASSERT_EQUAL_INT64(3, a.n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_names);
  RUN_TEST(test_rejects_empty);
  RUN_TEST(test_rejects_dotfile);
  RUN_TEST(test_rejects_double_dot);
  RUN_TEST(test_rejects_slashes);
  RUN_TEST(test_rejects_too_long);
  RUN_TEST(test_base64_basic);
  RUN_TEST(test_base64_no_padding);
  RUN_TEST(test_base64_two_pad);
  RUN_TEST(test_base64_bad_char);
  RUN_TEST(test_base64_bad_length);
  RUN_TEST(test_base64_output_overflow);
  RUN_TEST(test_begin_char_requires_idle);
  RUN_TEST(test_begin_char_rejects_bad_name);
  RUN_TEST(test_begin_char_rejects_over_budget);
  RUN_TEST(test_file_requires_char_open);
  RUN_TEST(test_chunk_requires_file_open);
  RUN_TEST(test_full_file_roundtrip);
  RUN_TEST(test_multiple_chunks_concat);
  RUN_TEST(test_end_file_fails_on_size_mismatch);
  RUN_TEST(test_bad_chunk_base64_fails);
  RUN_TEST(test_active_char_name);
  RUN_TEST(test_tick_empty_returns_none);
  RUN_TEST(test_queue_char_begin_deferred_until_tick);
  RUN_TEST(test_queue_while_pending_rejected);
  RUN_TEST(test_queue_chunk_not_written_until_tick);
  RUN_TEST(test_full_roundtrip_via_queue_tick);
  RUN_TEST(test_queue_file_begin_chunk_end_acks_carry_n);
  return UNITY_END();
}
