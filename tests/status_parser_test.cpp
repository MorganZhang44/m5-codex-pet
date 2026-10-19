// Host check: c++ -std=c++11 -Isrc tests/status_parser_test.cpp -o /tmp/pet-parser-test && /tmp/pet-parser-test
#include <cassert>
#include "status_parser.h"

int main() {
  struct Valid { const char* json; PetState expected; };
  const Valid valid[] = {
      {"{\"state\":\"SLEEP\"}", PetState::SLEEP},
      {"{\"state\":\"RUNNING\"}", PetState::RUNNING},
      {"{\"state\":\"SUCCESS\"}", PetState::SUCCESS},
      {" \r\n{ \"state\" \t: \"FAILED\" }\n", PetState::FAILED},
  };
  for (const auto& item : valid) {
    PetState state = PetState::SLEEP;
    assert(StatusParser::parse(item.json, state));
    assert(state == item.expected);
  }
  const char* invalid[] = {
      "", "{", "{}", "{\"state\":\"FAILED\"", "{\"state\":\"FAILED\"}garbage",
      "{\"state\":\"failed\"}", "{\"state\":\"UNKNOWN\"}", "{\"state\":null}",
      "{\"state\":\"FAILED\",\"state\":\"SUCCESS\"}",
      "{\"other\":\"FAILED\"}", "{\"state\":\"FAILED\",\"other\":1}",
      "{\"state\":\"FAILED\",}", "[{\"state\":\"FAILED\"}]",
      "{\"state\":\"FAILED\\\"\"}", "{\"state\":\"FAILED\"}\v",
  };
  for (const auto* json : invalid) {
    PetState state = PetState::RUNNING;
    assert(!StatusParser::parse(json, state));
    assert(state == PetState::RUNNING);  // Errors must preserve the caller's state.
  }
}
