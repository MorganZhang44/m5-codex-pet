#pragma once

#include <cstring>
#include "pet_state.h"

// Strict single-field service contract; reject extra fields, duplicates, unknown
// states and trailing data. Only JSON whitespace is accepted around tokens.
namespace StatusParser {
inline void skipSpace(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
}

inline bool consume(const char*& p, const char* token) {
  skipSpace(p);
  const auto length = std::strlen(token);
  if (std::strncmp(p, token, length) != 0) return false;
  p += length;
  return true;
}

inline bool parse(const char* json, PetState& state) {
  const char* p = json;
  if (!consume(p, "{") || !consume(p, "\"state\"") || !consume(p, ":")) {
    return false;
  }
  PetState parsed;
  if (consume(p, "\"SLEEP\"")) parsed = PetState::SLEEP;
  else if (consume(p, "\"RUNNING\"")) parsed = PetState::RUNNING;
  else if (consume(p, "\"SUCCESS\"")) parsed = PetState::SUCCESS;
  else if (consume(p, "\"FAILED\"")) parsed = PetState::FAILED;
  else return false;
  if (!consume(p, "}")) return false;
  skipSpace(p);
  if (*p != '\0') return false;
  state = parsed;
  return true;
}
}  // namespace StatusParser
