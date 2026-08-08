#pragma once

#include <string>
#include <vector>

#include "../ClippingEntry.h"

// Per-book clipping persistence plus the Kindle-compatible master export.
// The JSON copy is used by the on-device list; My Clippings.txt is append-only
// so it can be copied to another device without losing older highlights.
namespace ClipFile {

bool load(const std::string& bookPath, std::vector<ClippingEntry>& clippings);

// Replace the editable on-device list. The append-only My Clippings.txt
// export is intentionally left unchanged.
bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings);

// Saves the clipping in the per-book JSON file and appends it to
// /My Clippings.txt. Duplicate text at the same page is ignored.
bool append(const std::string& bookPath, const std::string& bookTitle, ClippingEntry clipping);

}  // namespace ClipFile
