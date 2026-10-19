#pragma once

#include <cstddef>
#include <string>

class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}

  String& operator=(const char* value) {
    value_ = value ? value : "";
    return *this;
  }

  String operator+(const char* suffix) const {
    return String(value_ + (suffix ? suffix : ""));
  }

  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
  bool concat(const char* suffix) {
    if (suffix) value_ += suffix;
    return true;
  }

 private:
  std::string value_;
};
