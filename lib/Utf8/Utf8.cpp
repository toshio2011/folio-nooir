#include "Utf8.h"

#include "Utf8ComposeTable.h"

namespace {
// Look up the canonical composition of (base + combining mark), or 0 if none.
uint32_t utf8ComposePair(const uint32_t base, const uint32_t mark) {
  if (base > 0xFFFF || mark > 0xFFFF) return 0;
  int lo = 0;
  int hi = kUtf8ComposeTableSize - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Utf8ComposeEntry& e = kUtf8ComposeTable[mid];
    if (e.base < base || (e.base == base && e.mark < mark)) {
      lo = mid + 1;
    } else if (e.base > base || (e.base == base && e.mark > mark)) {
      hi = mid - 1;
    } else {
      return e.composed;
    }
  }
  return 0;
}

bool isAsciiLetter(const uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

bool isLatinScript(const uint32_t cp) {
  return isAsciiLetter(cp) || (cp >= 0x00C0 && cp <= 0x02AF) || (cp >= 0x1E00 && cp <= 0x1EFF) ||
         (cp >= 0x2C60 && cp <= 0x2C7F) || (cp >= 0xA720 && cp <= 0xA7FF) || (cp >= 0xAB30 && cp <= 0xAB6F);
}

bool isArabicFamily(const uint32_t cp) {
  return (cp >= 0x0620 && cp <= 0x064A) || (cp >= 0x066D && cp <= 0x066F) || (cp >= 0x0671 && cp <= 0x06D5) ||
         (cp >= 0x06E5 && cp <= 0x06E6) || (cp >= 0x06EE && cp <= 0x06EF) || (cp >= 0x06FA && cp <= 0x06FF) ||
         (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x0870 && cp <= 0x0896) || (cp >= 0x08A0 && cp <= 0x08C9) ||
         (cp >= 0x08E3 && cp <= 0x08FF) || (cp >= 0x10EC0 && cp <= 0x10EF9) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
         (cp >= 0xFE70 && cp <= 0xFEFC) || (cp >= 0x1EE00 && cp <= 0x1EEFF);
}

bool isArabicCommon(const uint32_t cp) {
  return (cp >= 0x0600 && cp <= 0x061F) || (cp >= 0x0660 && cp <= 0x066C) || cp == 0x06DD || cp == 0x06DE ||
         cp == 0x06E9 || (cp >= 0x06F0 && cp <= 0x06F9) || (cp >= 0x0890 && cp <= 0x0891) || cp == 0x08E2;
}

bool isArabicMark(const uint32_t cp) {
  return (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
         (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) || (cp >= 0x06E7 && cp <= 0x06E8) ||
         (cp >= 0x06EA && cp <= 0x06ED) || (cp >= 0x0897 && cp <= 0x089F) || (cp >= 0x08CA && cp <= 0x08E1) ||
         (cp >= 0x08E3 && cp <= 0x08FF) || (cp >= 0x10EFA && cp <= 0x10EFF);
}

bool isHebrewScript(const uint32_t cp) {
  return (cp >= 0x05D0 && cp <= 0x05EA) || (cp >= 0x05F0 && cp <= 0x05F2);
}

uint8_t attachCommonRuns(Utf8ScriptRun* runs, const uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    if (runs[i].script != Utf8Script::Common) continue;

    Utf8Script previous = Utf8Script::Common;
    Utf8Script next = Utf8Script::Common;
    for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
      if (runs[j].script != Utf8Script::Common) {
        previous = runs[j].script;
        break;
      }
    }
    for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
      if (runs[j].script != Utf8Script::Common) {
        next = runs[j].script;
        break;
      }
    }
    if (previous != Utf8Script::Common && previous == next) {
      runs[i].script = previous;
    } else if (previous != Utf8Script::Common) {
      runs[i].script = previous;
    } else if (next != Utf8Script::Common) {
      runs[i].script = next;
    }
  }

  uint8_t compacted = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (compacted > 0 && runs[compacted - 1].script == runs[i].script) {
      const uint32_t end = runs[i].byteStart + runs[i].byteLength;
      runs[compacted - 1].byteLength = end - runs[compacted - 1].byteStart;
    } else {
      runs[compacted++] = runs[i];
    }
  }
  return compacted;
}
}  // namespace

std::string utf8ComposeNfc(const std::string& in) {
  // Fast path: NFC composition can only change text that contains a combining
  // diacritical mark U+0300-036F (UTF-8 lead byte 0xCC or 0xCD). Plain ASCII and
  // already-precomposed (NFC) text -- the vast majority of words -- have none, so
  // return them untouched without walking codepoints or allocating. A 0xCD that is
  // actually a non-combining codepoint just falls through to the full pass below.
  bool maybeHasMarks = false;
  for (const unsigned char c : in) {
    if (c == 0xCC || c == 0xCD) {
      maybeHasMarks = true;
      break;
    }
  }
  if (!maybeHasMarks) return in;

  std::string out;
  out.reserve(in.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in.c_str());
  uint32_t base = 0;
  bool haveBase = false;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;
    if (utf8IsCombiningMark(cp)) {
      const uint32_t composed = haveBase ? utf8ComposePair(base, cp) : 0;
      if (composed) {
        base = composed;  // keep accumulating further marks onto the composed char
        continue;
      }
      // No composition: flush the pending base, then emit the mark unchanged.
      if (haveBase) {
        utf8AppendCodepoint(base, out);
        haveBase = false;
      }
      utf8AppendCodepoint(cp, out);
    } else {
      if (haveBase) utf8AppendCodepoint(base, out);
      base = cp;
      haveBase = true;
    }
  }
  if (haveBase) utf8AppendCodepoint(base, out);
  return out;
}

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const unsigned char lead = **string;
  const int bytes = utf8CodepointLen(lead);
  const uint8_t* chr = *string;

  // Invalid lead byte (stray continuation byte 0x80-0xBF, or 0xFE/0xFF)
  if (bytes == 1 && lead >= 0x80) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  if (bytes == 1) {
    (*string)++;
    return chr[0];
  }

  // Validate continuation bytes before consuming them
  for (int i = 1; i < bytes; i++) {
    if ((chr[i] & 0xC0) != 0x80) {
      // Missing or invalid continuation byte — skip all bytes consumed so far
      *string += i;
      return REPLACEMENT_GLYPH;
    }
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  // Reject overlong encodings, surrogates, and out-of-range values
  const bool overlong = (bytes == 2 && cp < 0x80) || (bytes == 3 && cp < 0x800) || (bytes == 4 && cp < 0x10000);
  const bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);
  if (overlong || surrogate || cp > 0x10FFFF) {
    (*string)++;
    return REPLACEMENT_GLYPH;
  }

  *string += bytes;

  return cp;
}

void utf8AppendCodepoint(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

int utf8SafeTruncateBuffer(const char* buf, int len) {
  if (len <= 0) return 0;

  // Walk back past continuation bytes (10xxxxxx) to find the lead byte
  int leadPos = len - 1;
  while (leadPos > 0 && (static_cast<uint8_t>(buf[leadPos]) & 0xC0) == 0x80) {
    leadPos--;
  }

  // Determine expected length of the sequence starting at leadPos
  int expectedLen = utf8CodepointLen(static_cast<unsigned char>(buf[leadPos]));
  int actualLen = len - leadPos;

  if (actualLen < expectedLen && leadPos > 0) {
    // Incomplete UTF-8 sequence at the end — exclude it
    return leadPos;
  }
  return len;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

// Truncate string by removing N UTF-8 characters from the end
void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

Utf8Script utf8ClassifyScript(const uint32_t cp) {
  if (utf8IsTextMark(cp) || isArabicCommon(cp)) return Utf8Script::Common;
  if (isArabicFamily(cp)) return Utf8Script::Arabic;
  if (isHebrewScript(cp)) return Utf8Script::Hebrew;
  if (utf8IsCjkCodepoint(cp)) return Utf8Script::Cjk;
  if (isLatinScript(cp)) return Utf8Script::Latin;

  // ASCII digits and punctuation, Unicode punctuation, and symbols are
  // neutral. Unknown letters remain Other rather than being guessed into a
  // supported script's fallback run.
  if ((cp >= 0x0030 && cp <= 0x0039) || (cp >= 0x2000 && cp <= 0x206F) || cp <= 0x0040 ||
      (cp >= 0x005B && cp <= 0x0060) || (cp >= 0x007B && cp <= 0x007E)) {
    return Utf8Script::Common;
  }
  return Utf8Script::Other;
}

bool utf8IsArabicCodepoint(const uint32_t cp) {
  return isArabicFamily(cp) || isArabicCommon(cp) || isArabicMark(cp);
}

bool utf8ClassifyScriptRuns(const char* text, Utf8ScriptRun* runs, const uint8_t capacity, uint8_t* runCount,
                            bool* overflowed) {
  if (runCount) *runCount = 0;
  if (overflowed) *overflowed = false;
  if (!text || !runs || !runCount || capacity == 0) return false;

  const auto* begin = reinterpret_cast<const unsigned char*>(text);
  const auto* p = begin;
  uint8_t count = 0;
  while (*p) {
    const auto* cpStart = p;
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp) break;
    const Utf8Script script = utf8ClassifyScript(cp);
    const uint32_t byteStart = static_cast<uint32_t>(cpStart - begin);
    const uint32_t byteEnd = static_cast<uint32_t>(p - begin);

    if (count > 0 && runs[count - 1].script == script &&
        runs[count - 1].byteStart + runs[count - 1].byteLength == byteStart) {
      runs[count - 1].byteLength = byteEnd - runs[count - 1].byteStart;
      continue;
    }
    if (count >= capacity) {
      // Keep the result bounded. A Mixed suffix tells the next fallback stage
      // that it must not make a per-glyph allocation based on this run.
      runs[count - 1].script = Utf8Script::Mixed;
      while (*p) {
        utf8NextCodepoint(&p);
      }
      runs[count - 1].byteLength = static_cast<uint32_t>(p - begin) - runs[count - 1].byteStart;
      if (overflowed) *overflowed = true;
      break;
    }
    runs[count++] = Utf8ScriptRun{byteStart, byteEnd - byteStart, script};
  }

  *runCount = attachCommonRuns(runs, count);
  return true;
}

bool utf8ContainsRtlScript(const char* text) {
  if (!text) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    // Hebrew, Arabic core, and Syriac share these common two-byte lead ranges;
    // preserve the cheap fast path used by the renderer for ordinary text.
    if (*p >= 0xD6 && *p <= 0xDB) return true;

    // Arabic Supplement (DD), Extended-A/B (E0), presentation forms (EF),
    // and Extended-C/Arabic math (F0) require decoding to distinguish them
    // from neighboring scripts.
    if (*p == 0xDD || *p == 0xE0 || *p == 0xEF || *p == 0xF0) {
      const auto* before = p;
      const uint32_t cp = utf8NextCodepoint(&p);
      if (utf8ClassifyScript(cp) == Utf8Script::Arabic || utf8ClassifyScript(cp) == Utf8Script::Hebrew) return true;
      if (p == before) ++p;
      continue;
    }
    ++p;
  }
  return false;
}
