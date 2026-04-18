#include <unity.h>
#include "state.h"

static AppState freshConnected() {
  AppState s;
  s.mode = Mode::Connected;
  return s;
}

void test_first_heartbeat_without_prompt_enters_idle() {
  AppState s = freshConnected();
  HeartbeatData hb;
  hb.total = 3; hb.running = 1; hb.waiting = 0;
  hb.msg = "working";
  bool changed = applyHeartbeat(s, hb, 1000);
  TEST_ASSERT_TRUE(changed);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL(3, s.hb.total);
  TEST_ASSERT_EQUAL_UINT32(1000, s.lastHeartbeatMs);
}

void test_heartbeat_with_prompt_enters_prompt() {
  AppState s = freshConnected();
  HeartbeatData hb;
  hb.hasPrompt = true;
  hb.prompt.id = "req_abc";
  hb.prompt.tool = "Bash";
  hb.prompt.hint = "rm -rf /tmp/foo";
  applyHeartbeat(s, hb, 500);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Prompt), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL_STRING("req_abc", s.hb.prompt.id.c_str());
}

void test_prompt_cleared_by_heartbeat_returns_to_idle() {
  AppState s = freshConnected();
  HeartbeatData h1; h1.hasPrompt = true; h1.prompt.id = "req_abc";
  applyHeartbeat(s, h1, 100);
  HeartbeatData h2; h2.hasPrompt = false;
  applyHeartbeat(s, h2, 200);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
}

void test_button_a_in_prompt_requests_approve() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_xyz";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  bool sent = applyButton(s, 'A', 100, d, id);
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_EQUAL(static_cast<int>(PermissionDecision::Approve),
                    static_cast<int>(d));
  TEST_ASSERT_EQUAL_STRING("req_xyz", id.c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Ack), static_cast<int>(s.mode));
  TEST_ASSERT_TRUE(s.ackApproved);
}

void test_button_c_in_prompt_requests_deny() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_xyz";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  bool sent = applyButton(s, 'C', 100, d, id);
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_EQUAL(static_cast<int>(PermissionDecision::Deny),
                    static_cast<int>(d));
  TEST_ASSERT_FALSE(s.ackApproved);
}

void test_button_in_idle_does_nothing() {
  AppState s = freshConnected();
  HeartbeatData hb; applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  TEST_ASSERT_FALSE(applyButton(s, 'A', 100, d, id));
}

void test_ack_expires_to_idle() {
  AppState s = freshConnected();
  HeartbeatData hb; hb.hasPrompt = true; hb.prompt.id = "req_1";
  applyHeartbeat(s, hb, 0);
  PermissionDecision d; std::string id;
  applyButton(s, 'A', 100, d, id);
  // Not expired yet
  TEST_ASSERT_FALSE(applyTimeouts(s, 500));
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Ack), static_cast<int>(s.mode));
  // Expired (ACK_DISPLAY_MS = 1000)
  TEST_ASSERT_TRUE(applyTimeouts(s, 1200));
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Idle), static_cast<int>(s.mode));
}

void test_heartbeat_timeout_disconnects() {
  AppState s = freshConnected();
  HeartbeatData hb; applyHeartbeat(s, hb, 0);
  TEST_ASSERT_FALSE(applyTimeouts(s, 20000));
  TEST_ASSERT_TRUE(applyTimeouts(s, 40000));
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Disconnected),
                    static_cast<int>(s.mode));
}

void test_new_prompt_id_during_prompt_updates_without_ack() {
  AppState s = freshConnected();
  HeartbeatData h1; h1.hasPrompt = true; h1.prompt.id = "req_1"; h1.prompt.tool = "Bash";
  applyHeartbeat(s, h1, 0);
  HeartbeatData h2; h2.hasPrompt = true; h2.prompt.id = "req_2"; h2.prompt.tool = "Read";
  applyHeartbeat(s, h2, 100);
  TEST_ASSERT_EQUAL(static_cast<int>(Mode::Prompt), static_cast<int>(s.mode));
  TEST_ASSERT_EQUAL_STRING("req_2", s.hb.prompt.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Read", s.hb.prompt.tool.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_heartbeat_without_prompt_enters_idle);
  RUN_TEST(test_heartbeat_with_prompt_enters_prompt);
  RUN_TEST(test_prompt_cleared_by_heartbeat_returns_to_idle);
  RUN_TEST(test_button_a_in_prompt_requests_approve);
  RUN_TEST(test_button_c_in_prompt_requests_deny);
  RUN_TEST(test_button_in_idle_does_nothing);
  RUN_TEST(test_ack_expires_to_idle);
  RUN_TEST(test_heartbeat_timeout_disconnects);
  RUN_TEST(test_new_prompt_id_during_prompt_updates_without_ack);
  return UNITY_END();
}
