#pragma once

#include <Arduino.h>

#include <string>

namespace base64 {
inline String encode(const char* input) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const std::string value = input ? input : "";
  std::string encoded;
  for (size_t i = 0; i < value.size(); i += 3) {
    const unsigned a = static_cast<unsigned char>(value[i]);
    const unsigned b = i + 1 < value.size() ? static_cast<unsigned char>(value[i + 1]) : 0;
    const unsigned c = i + 2 < value.size() ? static_cast<unsigned char>(value[i + 2]) : 0;
    encoded += alphabet[a >> 2];
    encoded += alphabet[((a & 0x03u) << 4) | (b >> 4)];
    encoded += i + 1 < value.size() ? alphabet[((b & 0x0fu) << 2) | (c >> 6)] : '=';
    encoded += i + 2 < value.size() ? alphabet[c & 0x3fu] : '=';
  }
  return String(std::move(encoded));
}
}  // namespace base64
