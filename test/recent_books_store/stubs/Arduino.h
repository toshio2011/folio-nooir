#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

inline uint32_t micros() {
  static uint32_t value = 0;
  return value += 13;
}

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}
  String& operator=(const char* value) {
    value_ = value ? value : "";
    return *this;
  }
  String& operator=(const std::string& value) {
    value_ = value;
    return *this;
  }
  String operator+(const char* suffix) const { return String(value_ + (suffix ? suffix : "")); }
  bool concat(const char* suffix) {
    if (suffix) value_ += suffix;
    return true;
  }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }

 private:
  std::string value_;
};
