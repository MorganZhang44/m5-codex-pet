#pragma once
#include <cstdio>
#include <cstdarg>
#include <initializer_list>
#include <string>
#include <vector>

enum { TFT_BLACK, TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN, TFT_RED };
struct TestDisplay {
  std::vector<std::string> text;
  unsigned draws = 0;
  int width() const { return 135; }
  int height() const { return 240; }
  void setRotation(int) {}
  void setBrightness(int) {}
  void setTextFont(int) {}
  void setTextSize(int) {}
  void setTextColor(int, int) {}
  void setCursor(int, int) {}
  void fillScreen(int) { text.clear(); ++draws; }
  template<typename... T> void fillRect(T...) { ++draws; }
  template<typename... T> void drawRect(T...) { ++draws; }
  template<typename... T> void drawRoundRect(T...) { ++draws; }
  template<typename... T> void drawLine(T...) { ++draws; }
  template<typename... T> void drawFastHLine(T...) { ++draws; }
  template<typename... T> void fillCircle(T...) { ++draws; }
  template<typename... T> void drawCircle(T...) { ++draws; }
  void drawString(const char* s, int, int) { print(s); }
  void drawCenterString(const char* s, int, int) { print(s); }
  void drawRightString(const char* s, int, int) { print(s); }
  void print(const char* s) { text.emplace_back(s); ++draws; }
  void println(const char* s) { print(s); }
  void printf(const char* format, ...) {
    char value[128];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    print(value);
  }
};
struct TestM5 { TestDisplay Display; };
extern TestM5 M5;
