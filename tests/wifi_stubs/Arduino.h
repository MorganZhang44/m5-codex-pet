#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

constexpr int HEX = 16;
class String {
 public:
  String() = default;
  String(const char* value) : value_(value) {}
  String(const char* value, unsigned int length) : value_(value, length) {}
  String(uint32_t value, int) : value_(std::to_string(value)) {}
  bool isEmpty() const { return value_.empty(); }
  char operator[](size_t i) const { return value_[i]; }
  String& operator+=(const char* s) { value_ += s; return *this; }
  String& operator+=(char c) { value_ += c; return *this; }
  String& operator+=(const String& s) { value_ += s.value_; return *this; }
  friend String operator+(const String& a, const String& b) { return String((a.value_ + b.value_).c_str()); }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  void toCharArray(char* destination, size_t size) const {
    const auto count = std::min(size - 1, value_.size());
    std::memcpy(destination, value_.data(), count);
    destination[count] = 0;
  }
  friend String operator+(const char* prefix, const String& suffix) {
    return String((std::string(prefix) + suffix.value_).c_str());
  }
 private:
  std::string value_;
};
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
struct FakeESP {
  int restarts = 0;
  uint64_t getEfuseMac() const { return 123; }
  void restart() { ++restarts; }
};
extern FakeESP ESP;

struct FakeSerial {
  std::string input, output, pendingOutput;
  size_t offset = 0;
  void begin(int speed) { (void)speed; }
  int available() { return offset < input.size(); }
  int read() { return input[offset++]; }
  // Model asynchronous USB TX and HWCDC's disconnected flush path. A transient
  // connection check must not cause firmware to discard an acknowledgement.
  void println(const char* text) { pendingOutput += text; pendingOutput += '\n'; }
  void println(const String& text) { println(text.c_str()); }
  void flush() { pendingOutput.clear(); }
  void hostRead() { output += pendingOutput; pendingOutput.clear(); }
  void send(const std::string& text) { input += text; }
};
extern FakeSerial Serial;
