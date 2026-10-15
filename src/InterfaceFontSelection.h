#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>

namespace InterfaceFontSelection {

// A saved choice stays usable only while the family is still installed,
// provides every size-matched UI face, and fits the persisted family field.
// The caller can safely fall back to built-in UI when this predicate fails.
inline bool isCompatibleFamily(const SdCardFontFamilyInfo* family, const size_t selectionCapacity) {
  return family != nullptr && !family->name.empty() && family->name.size() < selectionCapacity &&
         family->hasInterfaceSizes();
}

}  // namespace InterfaceFontSelection
