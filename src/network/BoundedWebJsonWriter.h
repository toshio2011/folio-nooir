#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// A small JSON writer for streamed Web API responses. The writer owns no
// dynamic memory; the sink controls where bounded chunks are sent.
template <size_t BufferSize = 256>
class BoundedWebJsonWriter {
  static_assert(BufferSize > 0);

 public:
  using Sink = bool (*)(void* context, const char* bytes, size_t length);

  BoundedWebJsonWriter(Sink sink, void* context) : sink_(sink), context_(context) {}

  bool raw(const std::string_view value) {
    for (const char byte : value) {
      if (!append(byte)) return false;
    }
    return true;
  }

  bool string(const std::string_view value) {
    if (!append('"')) return false;
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char byte : value) {
      switch (byte) {
        case '"': if (!raw("\\\"")) return false; break;
        case '\\': if (!raw("\\\\")) return false; break;
        case '\b': if (!raw("\\b")) return false; break;
        case '\f': if (!raw("\\f")) return false; break;
        case '\n': if (!raw("\\n")) return false; break;
        case '\r': if (!raw("\\r")) return false; break;
        case '\t': if (!raw("\\t")) return false; break;
        default:
          if (byte < 0x20) {
            const char escaped[] = {'\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 0x0f]};
            if (!raw(std::string_view(escaped, sizeof(escaped)))) return false;
          } else if (!append(static_cast<char>(byte))) {
            return false;
          }
      }
    }
    return append('"');
  }

  bool unsignedNumber(uint64_t value) {
    char digits[20];
    size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10));
      value /= 10;
    } while (value != 0);
    while (count > 0) {
      if (!append(digits[--count])) return false;
    }
    return true;
  }

  bool signedNumber(int64_t value) {
    if (value >= 0) return unsignedNumber(static_cast<uint64_t>(value));
    if (!append('-')) return false;
    const uint64_t magnitude = static_cast<uint64_t>(-(value + 1)) + 1;
    return unsignedNumber(magnitude);
  }

  bool boolean(const bool value) { return raw(value ? "true" : "false"); }

  bool finish() { return flush(); }
  bool good() const { return good_; }
  size_t buffered() const { return used_; }

 private:
  bool append(const char byte) {
    if (!good_) return false;
    buffer_[used_++] = byte;
    return used_ != BufferSize || flush();
  }

  bool flush() {
    if (!good_) return false;
    if (used_ == 0) return true;
    good_ = sink_ && sink_(context_, buffer_.data(), used_);
    used_ = 0;
    return good_;
  }

  Sink sink_ = nullptr;
  void* context_ = nullptr;
  std::array<char, BufferSize> buffer_{};
  size_t used_ = 0;
  bool good_ = true;
};
