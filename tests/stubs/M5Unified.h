#pragma once
#include <cstdio>
#include <cstdarg>
#include <initializer_list>
#include <string>
#include <vector>

enum { TFT_BLACK, TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN, TFT_RED };
struct TestDisplay {
  struct TextDraw {
    std::string value;
    int x, y, font, size;
  };
  std::vector<std::string> text;
  std::vector<TextDraw> textDraws;
  unsigned draws = 0;
  int font = 1, size = 1, cursorX = 0, cursorY = 0;
  int width() const { return 135; }
  int height() const { return 240; }
  void setRotation(int) {}
  void setBrightness(int) {}
  void setTextFont(int value) { font = value; }
  void setTextSize(int value) { size = value; }
  void setTextColor(int, int) {}
  void setCursor(int x, int y) { cursorX = x; cursorY = y; }
  void fillScreen(int) { text.clear(); textDraws.clear(); ++draws; }
  template<typename... T> void fillRect(T...) { ++draws; }
  template<typename... T> void drawRect(T...) { ++draws; }
  template<typename... T> void drawRoundRect(T...) { ++draws; }
  template<typename... T> void drawLine(T...) { ++draws; }
  template<typename... T> void drawFastHLine(T...) { ++draws; }
  template<typename... T> void fillCircle(T...) { ++draws; }
  template<typename... T> void drawCircle(T...) { ++draws; }
  void drawString(const char* s, int x, int y) {
    text.emplace_back(s);
    textDraws.push_back({s, x, y, font, size});
    ++draws;
  }
  void drawCenterString(const char* s, int x, int y) {
    drawString(s, x - std::string(s).size() * 6 * size / 2, y);
  }
  void drawRightString(const char* s, int x, int y) {
    drawString(s, x - std::string(s).size() * 6 * size, y);
  }
  void print(const char* s) { drawString(s, cursorX, cursorY); }
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
