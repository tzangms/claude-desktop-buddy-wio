#include <unity.h>
#include "protocol.h"

void test_parse_heartbeat_basic() {
  std::string line = R"({"total":3,"running":1,"waiting":0,"msg":"working"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL(3, m.heartbeat.total);
  TEST_ASSERT_EQUAL(1, m.heartbeat.running);
  TEST_ASSERT_EQUAL_STRING("working", m.heartbeat.msg.c_str());
  TEST_ASSERT_FALSE(m.heartbeat.hasPrompt);
}

void test_parse_heartbeat_with_prompt() {
  std::string line = R"({"total":1,"running":0,"waiting":1,"msg":"approve: Bash","prompt":{"id":"req_abc","tool":"Bash","hint":"rm -rf /tmp/foo"}})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_TRUE(m.heartbeat.hasPrompt);
  TEST_ASSERT_EQUAL_STRING("req_abc", m.heartbeat.prompt.id.c_str());
  TEST_ASSERT_EQUAL_STRING("Bash", m.heartbeat.prompt.tool.c_str());
  TEST_ASSERT_EQUAL_STRING("rm -rf /tmp/foo", m.heartbeat.prompt.hint.c_str());
}

void test_parse_owner() {
  std::string line = R"({"cmd":"owner","name":"Felix"})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Owner),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("Felix", m.ownerName.c_str());
}

void test_parse_time() {
  std::string line = R"({"time":[1775731234,-25200]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Time),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_INT32(1775731234, m.timeEpoch);
  TEST_ASSERT_EQUAL_INT32(-25200, m.timeOffsetSec);
}

void test_parse_turn_event_is_unknown() {
  std::string line = R"({"evt":"turn","role":"assistant","content":[]})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Unknown),
                    static_cast<int>(m.kind));
}

void test_parse_malformed_is_error() {
  std::string line = R"({not json)";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::ParseError),
                    static_cast<int>(m.kind));
}

void test_format_permission_approve() {
  std::string out = formatPermission("req_abc", PermissionDecision::Approve);
  TEST_ASSERT_EQUAL_STRING(
      R"({"cmd":"permission","id":"req_abc","decision":"once"})" "\n",
      out.c_str());
}

void test_format_permission_deny() {
  std::string out = formatPermission("req_xyz", PermissionDecision::Deny);
  TEST_ASSERT_EQUAL_STRING(
      R"({"cmd":"permission","id":"req_xyz","decision":"deny"})" "\n",
      out.c_str());
}

void test_parse_heartbeat_missing_optional_fields() {
  std::string line = R"({"total":0,"running":0,"waiting":0})";
  ParsedMessage m = parseLine(line);
  TEST_ASSERT_EQUAL(static_cast<int>(MessageKind::Heartbeat),
                    static_cast<int>(m.kind));
  TEST_ASSERT_EQUAL_STRING("", m.heartbeat.msg.c_str());
  TEST_ASSERT_FALSE(m.heartbeat.hasPrompt);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_heartbeat_basic);
  RUN_TEST(test_parse_heartbeat_with_prompt);
  RUN_TEST(test_parse_owner);
  RUN_TEST(test_parse_time);
  RUN_TEST(test_parse_turn_event_is_unknown);
  RUN_TEST(test_parse_malformed_is_error);
  RUN_TEST(test_format_permission_approve);
  RUN_TEST(test_format_permission_deny);
  RUN_TEST(test_parse_heartbeat_missing_optional_fields);
  return UNITY_END();
}
