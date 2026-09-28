#include "SdCardFontRegistry.h"

bool SdCardFontFamilyInfo::hasSize(const uint8_t size) const {
  for (const auto& file : files) {
    if (file.pointSize == size) return true;
  }
  return false;
}
