#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Small JSON scanner for the per-book clipping store. It streams the root
// document and exposes one clipping object at a time, so callers can retain
// only the requested page instead of materializing the entire file.
namespace ClippingsJsonStream {

enum class RecordStatus : uint8_t { ignored, accepted, invalid };

struct Result {
  bool valid = false;
  bool hasClippingsArray = false;
};

namespace detail {

template <typename Reader>
class Parser {
 public:
  Parser(Reader& reader, char* recordBuffer, const size_t recordCapacity)
      : reader(reader), recordBuffer(recordBuffer), recordCapacity(recordCapacity) {}

  template <typename OnRecord>
  Result parse(OnRecord&& onRecord) {
    Result result;
    skipWhitespace();
    const int first = reader.read();
    if (first != '{') {
      if (first < 0 || !skipValue(first, 0) || !atEnd()) return result;
      result.valid = true;
      return result;
    }

    bool closed = false;
    while (!closed) {
      skipWhitespace();
      if (reader.peek() == '}') {
        reader.read();
        closed = true;
        break;
      }

      char key[24]{};
      size_t keyLength = 0;
      if (!readString(key, sizeof(key), keyLength)) return result;
      skipWhitespace();
      if (reader.read() != ':') return result;
      skipWhitespace();

      if (keyLength == sizeof("clippings") - 1 &&
          std::equal(key, key + keyLength, "clippings") && reader.peek() == '[') {
        if (result.hasClippingsArray || !parseClippingsArray(onRecord)) return result;
        result.hasClippingsArray = true;
      } else {
        const int value = reader.read();
        if (value < 0 || !skipValue(value, 0)) return result;
      }

      skipWhitespace();
      const int separator = reader.read();
      if (separator == '}') {
        closed = true;
      } else if (separator != ',') {
        return result;
      }
    }

    if (!closed || !atEnd()) return result;
    result.valid = true;
    return result;
  }

 private:
  static constexpr uint8_t kMaxDepth = 16;

  void skipWhitespace() {
    while (true) {
      const int c = reader.peek();
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return;
      reader.read();
    }
  }

  bool atEnd() {
    skipWhitespace();
    return reader.peek() < 0;
  }

  bool readString(char* output, const size_t outputCapacity, size_t& outputLength) {
    outputLength = 0;
    if (reader.read() != '"') return false;
    while (true) {
      const int c = reader.read();
      if (c < 0 || c < 0x20) return false;
      if (c == '"') return true;
      if (c != '\\') {
        appendKeyByte(output, outputCapacity, outputLength, static_cast<char>(c));
        continue;
      }

      const int escape = reader.read();
      if (escape < 0) return false;
      switch (escape) {
        case '"':
        case '\\':
        case '/': appendKeyByte(output, outputCapacity, outputLength, static_cast<char>(escape)); break;
        case 'b': appendKeyByte(output, outputCapacity, outputLength, '\b'); break;
        case 'f': appendKeyByte(output, outputCapacity, outputLength, '\f'); break;
        case 'n': appendKeyByte(output, outputCapacity, outputLength, '\n'); break;
        case 'r': appendKeyByte(output, outputCapacity, outputLength, '\r'); break;
        case 't': appendKeyByte(output, outputCapacity, outputLength, '\t'); break;
        case 'u': {
          uint32_t codepoint = 0;
          for (uint8_t i = 0; i < 4; ++i) {
            const int hex = reader.read();
            const int digit = hexDigit(hex);
            if (digit < 0) return false;
            codepoint = (codepoint << 4) | static_cast<uint32_t>(digit);
          }
          // Only ASCII keys can match the key used by this store. Preserve
          // other valid keys as an unmatched marker while consuming them.
          appendKeyByte(output, outputCapacity, outputLength,
                        codepoint <= 0x7F ? static_cast<char>(codepoint) : '?');
          break;
        }
        default: return false;
      }
    }
  }

  static void appendKeyByte(char* output, const size_t capacity, size_t& length, const char value) {
    if (capacity > 0 && length + 1 < capacity) output[length++] = value;
  }

  static int hexDigit(const int value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  }

  template <typename OnRecord>
  bool parseClippingsArray(OnRecord&& onRecord) {
    if (reader.read() != '[') return false;
    skipWhitespace();
    if (reader.peek() == ']') {
      reader.read();
      return true;
    }

    while (true) {
      skipWhitespace();
      if (reader.peek() == '{') {
        size_t length = 0;
        if (!captureObject(length)) return false;
        const RecordStatus status = onRecord(recordBuffer, length);
        if (status == RecordStatus::invalid) return false;
      } else {
        const int value = reader.read();
        if (value < 0 || !skipValue(value, 0)) return false;
      }

      skipWhitespace();
      const int separator = reader.read();
      if (separator == ']') return true;
      if (separator != ',') return false;
    }
  }

  bool captureObject(size_t& length) {
    length = 0;
    uint8_t stack[kMaxDepth]{};
    size_t depth = 0;
    bool inString = false;
    bool escaped = false;
    while (true) {
      const int c = reader.read();
      if (c < 0) return false;
      if (length + 1 >= recordCapacity) return false;
      recordBuffer[length++] = static_cast<char>(c);

      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          inString = false;
        } else if (c < 0x20) {
          return false;
        }
        continue;
      }

      if (c == '"') {
        inString = true;
      } else if (c == '{' || c == '[') {
        if (depth >= kMaxDepth) return false;
        stack[depth++] = static_cast<uint8_t>(c == '{' ? '}' : ']');
      } else if (c == '}' || c == ']') {
        if (depth == 0 || c != stack[depth - 1]) return false;
        if (--depth == 0) {
          recordBuffer[length] = '\0';
          return true;
        }
      }
    }
  }

  bool skipString() {
    return skipQuotedStringContents();
  }

  bool skipValue(const int first, const uint8_t depth) {
    if (depth > kMaxDepth) return false;
    if (first == '"') {
      // The opening quote has already been consumed.
      return skipQuotedStringContents();
    }
    if (first == '{') {
      skipWhitespace();
      if (reader.peek() == '}') {
        reader.read();
        return true;
      }
      while (true) {
        size_t keyLength = 0;
        char key[1]{};
        if (!readString(key, sizeof(key), keyLength)) return false;
        skipWhitespace();
        if (reader.read() != ':') return false;
        skipWhitespace();
        const int value = reader.read();
        if (value < 0 || !skipValue(value, depth + 1)) return false;
        skipWhitespace();
        const int separator = reader.read();
        if (separator == '}') return true;
        if (separator != ',') return false;
        skipWhitespace();
      }
    }
    if (first == '[') {
      skipWhitespace();
      if (reader.peek() == ']') {
        reader.read();
        return true;
      }
      while (true) {
        skipWhitespace();
        const int value = reader.read();
        if (value < 0 || !skipValue(value, depth + 1)) return false;
        skipWhitespace();
        const int separator = reader.read();
        if (separator == ']') return true;
        if (separator != ',') return false;
      }
    }

    char primitive[32]{};
    size_t length = 0;
    int c = first;
    while (c >= 0 && c != ',' && c != ']' && c != '}' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      if (length + 1 >= sizeof(primitive)) return false;
      primitive[length++] = static_cast<char>(c);
      c = reader.peek();
      if (c >= 0 && c != ',' && c != ']' && c != '}' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        reader.read();
      }
    }
    primitive[length] = '\0';
    return (length == 4 && std::equal(primitive, primitive + length, "true")) ||
           (length == 5 && std::equal(primitive, primitive + length, "false")) ||
           (length == 4 && std::equal(primitive, primitive + length, "null")) || isNumber(primitive, length);
  }

  bool skipQuotedStringContents() {
    while (true) {
      const int c = reader.read();
      if (c < 0 || c < 0x20) return false;
      if (c == '"') return true;
      if (c == '\\') {
        const int escape = reader.read();
        if (escape < 0) return false;
        if (escape == 'u') {
          for (uint8_t i = 0; i < 4; ++i) {
            if (hexDigit(reader.read()) < 0) return false;
          }
        } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' &&
                   escape != 'n' && escape != 'r' && escape != 't') {
          return false;
        }
      }
    }
  }

  static bool isNumber(const char* value, const size_t length) {
    if (length == 0) return false;
    size_t i = 0;
    if (value[i] == '-') ++i;
    if (i >= length) return false;
    if (value[i] == '0') {
      ++i;
    } else {
      if (value[i] < '1' || value[i] > '9') return false;
      while (i < length && value[i] >= '0' && value[i] <= '9') ++i;
    }
    if (i < length && value[i] == '.') {
      ++i;
      const size_t start = i;
      while (i < length && value[i] >= '0' && value[i] <= '9') ++i;
      if (i == start) return false;
    }
    if (i < length && (value[i] == 'e' || value[i] == 'E')) {
      ++i;
      if (i < length && (value[i] == '+' || value[i] == '-')) ++i;
      const size_t start = i;
      while (i < length && value[i] >= '0' && value[i] <= '9') ++i;
      if (i == start) return false;
    }
    return i == length;
  }

  Reader& reader;
  char* recordBuffer;
  size_t recordCapacity;
};

}  // namespace detail

template <typename Reader, typename OnRecord>
Result parse(Reader& reader, char* recordBuffer, const size_t recordCapacity, OnRecord&& onRecord) {
  detail::Parser<Reader> parser(reader, recordBuffer, recordCapacity);
  return parser.parse(static_cast<OnRecord&&>(onRecord));
}

}  // namespace ClippingsJsonStream
