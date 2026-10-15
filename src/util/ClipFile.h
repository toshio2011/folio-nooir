#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../ClippingEntry.h"

// Per-book clipping persistence plus the Kindle-compatible master export.
// The JSON copy is used by the on-device list; My Clippings.txt is append-only
// so it can be copied to another device without losing older highlights.
namespace ClipFile {

// Notes are optional on-disk metadata and are kept small for the device's
// limited heap. The byte cap is applied on both load and save boundaries.
constexpr size_t MAX_NOTE_BYTES = 256;

// Converts malformed UTF-8 and glyphs that cannot be rendered reliably by
// the e-ink font set to ordinary spaces. Normal ASCII question marks remain
// unchanged. This is used at both persistence and display boundaries so an
// old clipping cannot reintroduce a replacement diamond later.
std::string normalizeText(const std::string& text);

bool load(const std::string& bookPath, std::vector<ClippingEntry>& clippings);
// Loads notes alongside entries for annotation-manager screens. The notes
// vector has one item per clipping; the reader can keep using the overload
// above without loading or retaining note text.
bool load(const std::string& bookPath, std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes);

// Replace the editable on-device list. The append-only My Clippings.txt
// export is intentionally left unchanged.
bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings);
// Replace the editable list and its aligned notes. Empty notes are omitted
// from JSON so existing readers continue to see the original entry shape.
bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings,
             const std::vector<std::string>& notes);

// Saves the clipping in the per-book JSON file and appends it to
// /My Clippings.txt. Duplicate text at the same page is ignored.
bool append(const std::string& bookPath, const std::string& bookTitle, ClippingEntry clipping);

}  // namespace ClipFile
