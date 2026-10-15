#pragma once

#include "Print.h"

#include <string>
#include <utility>

class String {
 public:
  String() = default;
  String(const char* value) : value(value ? value : "") {}
  String(std::string value) : value(std::move(value)) {}
  const char* c_str() const { return value.c_str(); }

 private:
  std::string value;
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int peek() = 0;
  virtual int read() = 0;
};
