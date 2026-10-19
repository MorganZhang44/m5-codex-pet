#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "status_data.h"

// Strict flat whitelist: all eight fields exactly once, in any order. Parse into
// a temporary so invalid replies cannot partially update the caller's snapshot.
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

inline bool digit(char c) { return c >= '0' && c <= '9'; }

inline bool number(const char*& p, double& value) {
  skipSpace(p);
  const char* start = p;
  if (*p == '-') ++p;
  if (*p == '0') ++p;
  else {
    if (*p < '1' || *p > '9') return false;
    while (digit(*p)) ++p;
  }
  if (*p == '.') {
    ++p;
    if (!digit(*p)) return false;
    while (digit(*p)) ++p;
  }
  if (*p == 'e' || *p == 'E') {
    ++p;
    if (*p == '+' || *p == '-') ++p;
    if (!digit(*p)) return false;
    while (digit(*p)) ++p;
  }
  char* end = nullptr;
  errno = 0;
  value = std::strtod(start, &end);
  return end == p && errno != ERANGE && std::isfinite(value);
}

// Validate a JSON string fully, optionally retaining decoded ASCII for ISO parsing.
// Non-ASCII/control characters cannot occur in a usable ISO reset timestamp.
inline bool jsonString(const char*& p, char* output = nullptr, size_t capacity = 0) {
  if (!consume(p, "\"")) return false;
  size_t length = 0;
  while (*p && *p != '"') {
    unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x20) return false;
    if (c == '\\') {
      if (*p == 'u') {
        ++p;
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
          const char hex = *p;
          if (!digit(hex) && !(hex >= 'a' && hex <= 'f') &&
              !(hex >= 'A' && hex <= 'F')) return false;
          code = code * 16 + (digit(hex) ? hex - '0' :
                             hex >= 'a' ? hex - 'a' + 10 : hex - 'A' + 10);
          ++p;
        }
        c = code >= 0x20 && code < 0x7f ? static_cast<unsigned char>(code) : '?';
      } else {
        if (!*p || !std::strchr("\"\\/bfnrt", *p)) return false;
        c = static_cast<unsigned char>(*p++);
        if (std::strchr("bfnrt", c)) c = '?';
      }
    }
    if (output && length + 1 < capacity) output[length] = static_cast<char>(c);
    ++length;
  }
  if (!consume(p, "\"")) return false;
  if (output && capacity) output[length < capacity ? length : 0] = '\0';
  return true;
}

// The bridge's usage_stale boolean alone controls freshness.
inline bool timestamp(const char*& p) {
  if (consume(p, "null")) return true;
  skipSpace(p);
  if (*p == '"') return jsonString(p);
  double ignored;
  return number(p, ignored);
}

inline bool resetTimestamp(const char*& p, UsageReset& reset) {
  if (consume(p, "null")) return true;
  char text[513] = {};  // Covers the existing transport's entire 512-byte body.
  if (!jsonString(p, text, sizeof(text))) return false;
  reset = parseUsageReset(text);
  return true;
}

inline bool percent(const char*& p, double& value) {
  if (consume(p, "null")) {
    value = -1;
    return true;
  }
  return number(p, value) && value >= 0 && value <= 100;
}

inline bool parse(const char* json, StatusData& status) {
  if (!json) return false;
  const char* p = json;
  if (!consume(p, "{")) return false;
  StatusData parsed;
  unsigned seen = 0;
  for (;;) {
    unsigned field;
    if (consume(p, "\"state\"")) field = 1;
    else if (consume(p, "\"usage_5h_percent\"")) field = 2;
    else if (consume(p, "\"usage_7d_percent\"")) field = 4;
    else if (consume(p, "\"usage_observed_at\"")) field = 8;
    else if (consume(p, "\"usage_stale\"")) field = 16;
    else if (consume(p, "\"last_result\"")) field = 32;
    else if (consume(p, "\"usage_5h_resets_at\"")) field = 64;
    else if (consume(p, "\"usage_7d_resets_at\"")) field = 128;
    else return false;
    if ((seen & field) || !consume(p, ":")) return false;
    seen |= field;
    if (field == 1) {
      if (consume(p, "\"SLEEP\"")) parsed.state = PetState::SLEEP;
      else if (consume(p, "\"RUNNING\"")) parsed.state = PetState::RUNNING;
      else if (consume(p, "\"SUCCESS\"")) parsed.state = PetState::SUCCESS;
      else if (consume(p, "\"FAILED\"")) parsed.state = PetState::FAILED;
      else return false;
    } else if (field == 2) {
      if (!percent(p, parsed.usage.fiveHour)) return false;
    } else if (field == 4) {
      if (!percent(p, parsed.usage.sevenDay)) return false;
    } else if (field == 8) {
      if (!timestamp(p)) return false;
    } else if (field == 64 || field == 128) {
      if (!resetTimestamp(p, field == 64 ? parsed.usage.fiveHourReset :
                                         parsed.usage.sevenDayReset)) return false;
    } else if (field == 32) {
      if (consume(p, "null")) parsed.lastResult = LastResult::NONE;
      else if (consume(p, "\"SUCCESS\"")) parsed.lastResult = LastResult::SUCCESS;
      else if (consume(p, "\"FAILED\"")) parsed.lastResult = LastResult::FAILED;
      else return false;
    } else {
      if (consume(p, "true")) parsed.usage.stale = true;
      else if (consume(p, "false")) parsed.usage.stale = false;
      else return false;
    }
    if (consume(p, "}")) break;
    if (!consume(p, ",")) return false;
  }
  skipSpace(p);
  if (seen != 255 || *p != '\0') return false;
  status = parsed;
  return true;
}
}  // namespace StatusParser
