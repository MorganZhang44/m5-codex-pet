#pragma once

#include <cstdio>
#include <cstring>

// Only the UTC components needed by the compact display; no device clock.
struct UsageReset {
  int hour = -1;
  int minute = -1;
  int weekday = -1;  // Monday = 0.

  bool operator==(const UsageReset& other) const {
    return hour == other.hour && minute == other.minute && weekday == other.weekday;
  }

  void label(char* output, size_t size, bool fiveHour) const {
    static const char* const days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if (weekday < 0) std::snprintf(output, size, "R--");
    else if (fiveHour) std::snprintf(output, size, "R%02d:%02d", hour, minute);
    else std::snprintf(output, size, "R%s", days[weekday]);
  }
};

inline UsageReset parseUsageReset(const char* text) {
  UsageReset result;
  const size_t length = std::strlen(text);
  if (length < 20) return result;
  for (size_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7) { if (text[i] != '-') return result; }
    else if (i == 10) { if (text[i] != 'T') return result; }
    else if (i == 13 || i == 16) { if (text[i] != ':') return result; }
    else if (text[i] < '0' || text[i] > '9') return result;
  }
  size_t end = 19;
  if (text[end] == '.') {
    const size_t start = ++end;
    while (text[end] >= '0' && text[end] <= '9') ++end;
    if (end == start) return result;
  }
  if (std::strcmp(text + end, "Z") != 0 &&
      std::strcmp(text + end, "+00:00") != 0) return result;
  const int year = (text[0]-'0')*1000 + (text[1]-'0')*100 + (text[2]-'0')*10 + text[3]-'0';
  const auto pair = [text](int i) { return (text[i]-'0')*10 + text[i+1]-'0'; };
  const int month = pair(5), day = pair(8), hour = pair(11), minute = pair(14);
  if (year == 0 || month < 1 || month > 12 || hour > 23 || minute > 59 || pair(17) > 59)
    return result;
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (day < 1 || day > days[month-1] + (month == 2 && leap ? 1 : 0)) return result;
  // Gregorian weekday calculation, independent of time_t, timezone, and NTP.
  static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  const int y = year - (month < 3 ? 1 : 0);
  result.weekday = (y + y/4 - y/100 + y/400 + offsets[month-1] + day + 6) % 7;
  result.hour = hour;
  result.minute = minute;
  return result;
}
