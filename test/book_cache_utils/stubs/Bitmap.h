#pragma once
#include <cstdint>
#include <HalStorage.h>
enum class BmpReaderError : uint8_t { Ok, FileInvalid };
class Bitmap {
 public:
  explicit Bitmap(HalFile&) {}
  BmpReaderError parseHeaders() { return BmpReaderError::FileInvalid; }
  int getRowBytes() const { return 0; }
  int getHeight() const { return 0; }
  int getWidth() const { return 0; }
};
