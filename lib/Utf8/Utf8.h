#pragma once

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Canonical composition (NFC) for the Latin / Vietnamese range: precomposes a
// base letter followed by combining diacritical mark(s) into a single codepoint.
// Needed because the device fonts have no combining-mark positioning, so text
// stored in NFD (e.g. some EPUB chapter titles) otherwise renders broken.
std::string utf8ComposeNfc(const std::string& in);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns true for CJK characters that allow line breaks on either side without hyphenation.
// Covers CJK Unified Ideographs, Hiragana, Katakana, Hangul Syllables, CJK punctuation,
// and fullwidth forms — the ranges where word boundaries are implicit per character.
inline bool utf8IsCjkBreakable(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x3000 && cp <= 0x303F)     // CJK Symbols and Punctuation
         || (cp >= 0x3040 && cp <= 0x309F)     // Hiragana
         || (cp >= 0x30A0 && cp <= 0x30FF)     // Katakana
         || (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul Syllables
         || (cp >= 0xD7B0 && cp <= 0xD7FF)     // Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2A6DF)   // CJK Extension B
         || (cp >= 0x2A700 && cp <= 0x2B73F);  // CJK Extension C
}

// Returns true for any codepoint in a CJK script block (Han, Kana, Hangul, Bopomofo,
// radicals, and CJK punctuation/compatibility/enclosed forms). Used for fallback font
// selection — deliberately broader than utf8IsCjkBreakable, whose ranges are tuned to
// implicit line-break opportunities and must not grow without rethinking layout.
inline bool utf8IsCjkCodepoint(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x2E80 && cp <= 0x2FDF)     // CJK Radicals Supplement, Kangxi Radicals
         || (cp >= 0x3000 && cp <= 0x33FF)     // CJK punctuation, Kana, Bopomofo, Hangul Compat
                                               // Jamo, Kanbun, strokes, enclosed + compat forms
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xA960 && cp <= 0xA97F)     // Hangul Jamo Extended-A
         || (cp >= 0xAC00 && cp <= 0xD7FF)     // Hangul Syllables, Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE10 && cp <= 0xFE1F)     // Vertical Forms
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2EBEF)   // CJK Extensions B-F
         || (cp >= 0x2F800 && cp <= 0x2FA1F)   // CJK Compatibility Ideographs Supplement
         || (cp >= 0x30000 && cp <= 0x323AF);  // CJK Extensions G-H
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)      // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)   // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)   // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F);  // Combining Half Marks
}

// Explicit RTL-script non-spacing marks used by Hebrew and Arabic-family
// writing systems. Keep this list bounded and data-driven: unknown Unicode
// marks must not silently become zero-width text.
inline bool utf8IsTransparentMark(const uint32_t cp) {
  return (cp >= 0x0591 && cp <= 0x05BD)   // Hebrew accents and vowel points
         || cp == 0x05BF                 // Hebrew point rafe
         || (cp >= 0x05C1 && cp <= 0x05C2)  // Hebrew shin/sin dots
         || (cp >= 0x05C4 && cp <= 0x05C5)  // Hebrew upper/lower dots
         || cp == 0x05C7                 // Hebrew point qamats qatan
         || (cp >= 0x0610 && cp <= 0x061A)  // Arabic honorifics/small marks
         || (cp >= 0x064B && cp <= 0x065F)  // Arabic harakat
         || cp == 0x0670                 // Arabic superscript alef
         || (cp >= 0x06D6 && cp <= 0x06DC)  // Quranic annotation marks
         || (cp >= 0x06DF && cp <= 0x06E4)
         || (cp >= 0x06E7 && cp <= 0x06E8)
         || (cp >= 0x06EA && cp <= 0x06ED)
         || (cp >= 0x0897 && cp <= 0x089F)  // Arabic Extended-B marks
         || (cp >= 0x08CA && cp <= 0x08E1)  // Arabic Extended-A marks
         || (cp >= 0x08E3 && cp <= 0x08FF)
         || (cp >= 0x10EFA && cp <= 0x10EFF);  // Arabic Extended-C marks
}

// The complete bounded text-mark path used by drawing and measurements.
// Generic combining ranges retain their existing behaviour; the explicit RTL
// ranges above add only known transparent marks.
inline bool utf8IsTextMark(const uint32_t cp) {
  return utf8IsCombiningMark(cp) || utf8IsTransparentMark(cp);
}

enum class Utf8Script : uint8_t {
  Common,
  Latin,
  Arabic,
  Hebrew,
  Cjk,
  Other,
  Mixed,
};

struct Utf8ScriptRun {
  uint32_t byteStart = 0;
  uint32_t byteLength = 0;
  Utf8Script script = Utf8Script::Common;
};

inline constexpr uint8_t UTF8_MAX_SCRIPT_RUNS = 16;

Utf8Script utf8ClassifyScript(uint32_t cp);

// Returns true for Arabic letters, punctuation, presentation forms, and the
// bounded Arabic transparent-mark ranges used by the reader fallback path.
bool utf8IsArabicCodepoint(uint32_t cp);

// Resolve a UTF-8 string into a bounded set of script runs. Common
// punctuation, numbers, spaces, and marks are attached to the nearest useful
// script (prefer the preceding script when both sides differ). If the caller's
// capacity is exceeded, the remaining suffix is conservatively returned as a
// single Mixed run and overflowed is set.
bool utf8ClassifyScriptRuns(const char* text, Utf8ScriptRun* runs, uint8_t capacity, uint8_t* runCount,
                            bool* overflowed = nullptr);

// Cheap RTL preflight used to preserve the renderer's Latin-only fast path.
// It recognizes the common two-byte RTL ranges by lead byte and decodes only
// the three-/four-byte leads that can contain extended Arabic blocks.
bool utf8ContainsRtlScript(const char* text);
