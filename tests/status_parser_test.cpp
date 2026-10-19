// Host check: c++ -std=c++11 -Isrc tests/status_parser_test.cpp -o /tmp/pet-parser-test && /tmp/pet-parser-test
#include <cassert>
#include <string>
#include "status_parser.h"

static std::string payload(const std::string& state = "\"RUNNING\"",
                           const std::string& five = "42.5",
                           const std::string& seven = "68",
                           const std::string& observed = "1792418400",
                           const std::string& stale = "false",
                           const std::string& result = "null",
                           const std::string& fiveReset = "null",
                           const std::string& sevenReset = "null") {
  return "{\"state\":" + state + ",\"usage_5h_percent\":" + five +
         ",\"usage_7d_percent\":" + seven + ",\"usage_observed_at\":" +
         observed + ",\"usage_stale\":" + stale + ",\"last_result\":" + result +
         ",\"usage_5h_resets_at\":" + fiveReset +
         ",\"usage_7d_resets_at\":" + sevenReset + "}";
}

static void rejects(const std::string& json) {
  StatusData status;
  status.state = PetState::SUCCESS;
  status.lastResult = LastResult::FAILED;
  status.usage.fiveHour = 17.25;
  status.usage.sevenDay = 99;
  status.usage.stale = true;
  status.usage.fiveHourReset = parseUsageReset("2026-10-19T18:22:00Z");
  status.usage.sevenDayReset = parseUsageReset("2026-10-20T09:30:00Z");
  const StatusData before = status;
  assert(!StatusParser::parse(json.c_str(), status));
  assert(status.state == before.state);
  assert(status.lastResult == before.lastResult);
  assert(status.usage == before.usage);
}

int main() {
  const char* names[] = {"SLEEP", "RUNNING", "SUCCESS", "FAILED"};
  const PetState states[] = {PetState::SLEEP, PetState::RUNNING,
                            PetState::SUCCESS, PetState::FAILED};
  StatusData status;
  assert(status.usage.fiveHour == -1 && status.usage.sevenDay == -1);
  for (int i = 0; i < 4; ++i) {
    assert(StatusParser::parse(payload(std::string("\"") + names[i] + "\"").c_str(), status));
    assert(status.state == states[i]);
    assert(status.usage.fiveHour == 42.5 && status.usage.sevenDay == 68);
    assert(!status.usage.stale);
  }
  for (const auto* timestamp : {"1792418400.125", "1.7924184e12",
                               "\"2026-10-19T14:00:00Z\"", "\"1792418400\"",
                               "\"escaped\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0030\"", "null"}) {
    assert(StatusParser::parse(payload("\"SLEEP\"", "0", "100", timestamp, "true").c_str(), status));
    assert(status.usage.fiveHour == 0 && status.usage.sevenDay == 100);
    assert(status.usage.stale);
  }
  assert(StatusParser::parse(payload("\"FAILED\"", "null", "null", "null", "true").c_str(), status));
  assert(status.usage.fiveHour == -1 && status.usage.sevenDay == -1 && status.usage.stale);
  assert(StatusParser::parse(payload("\"SUCCESS\"", "null", "1e2").c_str(), status));
  assert(status.usage.fiveHour == -1 && status.usage.sevenDay == 100 && !status.usage.stale);
  assert(StatusParser::parse(payload("\"SUCCESS\"", "-0", "null").c_str(), status));
  assert(status.usage.fiveHour == 0 && status.usage.sevenDay == -1);
  assert(StatusParser::parse(" \r\n{ \"usage_stale\" : false, \"usage_observed_at\":\"date\",\n"
                             "\"usage_7d_percent\": 25.125,\"state\"\t: \"RUNNING\","
                             "\"usage_5h_percent\": 5E-1, \"last_result\":null, "
                             "\"usage_7d_resets_at\":null, \"usage_5h_resets_at\":null }\t", status));
  assert(status.state == PetState::RUNNING && status.usage.fiveHour == .5 && status.usage.sevenDay == 25.125);
  const Usage beforeTimestampChange = status.usage;
  assert(StatusParser::parse(payload("\"RUNNING\"", "0.5", "25.125", "\"new date\"").c_str(), status));
  assert(status.usage == beforeTimestampChange);

  for (const auto* value : {"-1", "100.1", "101", "\"42\"", "true", "false", "{}", "[]",
                            "NaN", "Infinity", "1e999", "1e-999", "+1", "01", ".5", "1.", "1e", "--1", "nullx"}) {
    rejects(payload("\"SLEEP\"", value));
    rejects(payload("\"SLEEP\"", "1", value));
  }
  for (const auto* value : {"null", "1", "\"true\"", "True", "falsex", "{}"}) {
    rejects(payload("\"SLEEP\"", "1", "2", "null", value));
  }
  for (const auto* value : {"true", "{}", "[]", "NaN", "1e999", "\"bad\\x\"",
                            "\"bad\\u12\"", "\"bad\\uZZZZ\"", "\"raw\nnewline\"", "\"unfinished", "\"bad\\"}) {
    rejects(payload("\"SLEEP\"", "1", "2", value));
  }
  for (const auto* value : {"\"sleep\"", "\"UNKNOWN\"", "null", "42", "\"FAILED\\\"\""}) {
    rejects(payload(value));
  }
  for (const auto* value : {"\"SLEEP\"", "\"RUNNING\"", "\"success\"", "true", "42", "{}", "nullx"}) {
    rejects(payload("\"SLEEP\"", "1", "2", "null", "false", value));
  }
  for (const auto* result : {"null", "\"SUCCESS\"", "\"FAILED\""}) {
    for (int i = 0; i < 4; ++i) {
      assert(StatusParser::parse(payload(std::string("\"") + names[i] + "\"", "1", "2", "null", "false", result).c_str(), status));
      assert(status.state == states[i]);
      assert(status.lastResult == (std::string(result) == "null" ? LastResult::NONE :
             std::string(result) == "\"SUCCESS\"" ? LastResult::SUCCESS : LastResult::FAILED));
    }
  }
  // Reset strings are display metadata: invalid dates fall back, invalid JSON rejects.
  for (const auto* value : {"42", "true", "false", "{}", "[]", "nullx",
                            "\"bad\\x\"", "\"bad\\u12\"", "\"bad\\uZZZZ\"",
                            "\"raw\nnewline\"", "\"unfinished", "\"bad\\"}) {
    rejects(payload("\"RUNNING\"", "1", "2", "null", "false", "null", value));
    rejects(payload("\"RUNNING\"", "1", "2", "null", "false", "null", "null", value));
  }
  for (const auto* value : {"null", "\"\"", "\"unavailable\"", "\"2026-02-29T18:22:00Z\"",
                            "\"2026-10-19T24:00:00Z\"", "\"2026-10-19T18:22:00+01:00\""}) {
    assert(StatusParser::parse(payload("\"SLEEP\"", "1", "2", "null", "true", "\"FAILED\"", value, value).c_str(), status));
    assert(status.usage.fiveHourReset == UsageReset{});
    assert(status.usage.sevenDayReset == UsageReset{});
    assert(status.usage.stale && status.lastResult == LastResult::FAILED);
  }
  assert(StatusParser::parse(payload("\"RUNNING\"", "42.5", "68", "null", "false", "null",
      "\"2026-10-19T18:22:00.123Z\"", "\"2026-10-19T09:30:00+00:00\"").c_str(), status));
  char label[7];
  status.usage.fiveHourReset.label(label, sizeof(label), true);
  assert(std::string(label) == "R18:22");
  status.usage.sevenDayReset.label(label, sizeof(label), false);
  assert(std::string(label) == "RMon");
  Usage changedReset = status.usage;
  changedReset.fiveHourReset.minute = 23;
  assert(!(changedReset == status.usage));
  changedReset = status.usage;
  changedReset.sevenDayReset.weekday = 1;
  assert(!(changedReset == status.usage));
  const Usage beforeResetChange = status.usage;
  assert(StatusParser::parse(payload("\"RUNNING\"", "42.5", "68", "null", "false", "null",
      "\"2026-10-19T18:23:00Z\"", "\"2026-10-20T09:30:00Z\"").c_str(), status));
  assert(!(status.usage == beforeResetChange));
  assert(!status.usage.stale);  // Reset never determines freshness.
  assert(StatusParser::parse(payload("\"RUNNING\"", "1", "2", "null", "false", "null",
      "\"2026-10-19T18:22:00\\u005a\"", "\"2026-10-19T09:30:00Z\"").c_str(), status));
  status.usage.fiveHourReset.label(label, sizeof(label), true);
  assert(std::string(label) == "R18:22");
  for (const std::string& value : {std::string("\"2026-10-19T18:22:00Z\\u0000junk\""),
                                   std::string("\"2026-10-19T18:22:00Z\\n\""),
                                   "\"" + std::string(600, 'x') + "\""}) {
    assert(StatusParser::parse(payload("\"SLEEP\"", "1", "2", "null", "false", "null", value).c_str(), status));
    assert(status.usage.fiveHourReset == UsageReset{});
  }
  const char* weekdays[] = {"RMon", "RTue", "RWed", "RThu", "RFri", "RSat", "RSun"};
  for (int day = 19; day <= 25; ++day) {
    const std::string date = "2026-10-" + std::to_string(day) + "T00:00:00Z";
    parseUsageReset(date.c_str()).label(label, sizeof(label), false);
    assert(std::string(label) == weekdays[day - 19]);
  }
  for (const auto* date : {"2000-02-29T23:59:59Z", "2024-02-29T00:00:00Z",
                           "2026-12-31T23:59:59Z", "2027-01-01T00:00:00Z"}) {
    assert(parseUsageReset(date).weekday >= 0);
  }
  assert(parseUsageReset("2000-02-29T23:59:59Z").weekday == 1);
  assert(parseUsageReset("2027-01-01T00:00:00Z").weekday == 4);
  for (const auto* date : {"1900-02-29T00:00:00Z", "0000-01-01T00:00:00Z",
                           "2026-00-19T18:22:00Z", "2026-13-19T18:22:00Z",
                           "2026-10-00T18:22:00Z", "2026-04-31T18:22:00Z",
                           "2026-10-19T18:60:00Z", "2026-10-19T18:22:60Z",
                           "2026-10-19T18:22:00.Z", "2026-10-19T18:22:00",
                           "2026-10-19 18:22:00Z", "2026-10-19T18:22:00Zjunk"}) {
    const UsageReset reset = parseUsageReset(date);
    assert(reset == UsageReset{});
    reset.label(label, sizeof(label), true);
    assert(std::string(label) == "R--");
    reset.label(label, sizeof(label), false);
    assert(std::string(label) == "R--");
  }
  const std::string withResets = payload("\"RUNNING\"", "42.5", "68", "null", "false", "null",
      "\"2026-10-19T18:22:00Z\"", "\"2026-10-19T09:30:00Z\"");
  for (size_t length = 0; length < withResets.size(); ++length)
    rejects(withResets.substr(0, length));
  const std::string valid = payload();
  for (const auto* json : {"", "{", "{}", "[]", "{\"state\":\"SLEEP\"}"}) rejects(json);
  rejects(valid + "garbage");
  rejects(valid + "\v");
  rejects("[" + valid + "]");
  rejects(valid.substr(0, valid.size() - 1) + ",}");
  rejects(valid.substr(0, valid.size() - 1) + ",\"extra\":1}");
  const char* fields[] = {"\"state\":\"FAILED\"", "\"usage_5h_percent\":null",
                          "\"usage_7d_percent\":2", "\"usage_observed_at\":null",
                          "\"usage_stale\":true", "\"last_result\":null",
                          "\"usage_5h_resets_at\":null", "\"usage_7d_resets_at\":null"};
  for (const auto* field : fields) {
    rejects(valid.substr(0, valid.size() - 1) + "," + field + "}");
  }
  // Every required field must be present; every truncated prefix must fail.
  for (const auto* missing : {"state", "usage_5h_percent", "usage_7d_percent", "usage_observed_at", "usage_stale", "last_result", "usage_5h_resets_at", "usage_7d_resets_at"}) {
    std::string json = "{";
    for (const auto* field : fields) {
      if (std::string(field).find(std::string("\"") + missing + "\":") == 0) continue;
      if (json.size() > 1) json += ',';
      json += field;
    }
    rejects(json + "}");
  }
  for (size_t length = 0; length < valid.size(); ++length) rejects(valid.substr(0, length));
  const StatusData before = status;
  assert(!StatusParser::parse(nullptr, status));
  assert(status.state == before.state && status.usage == before.usage && status.lastResult == before.lastResult);
}
