#include "BidiUtils.h"

extern "C" {
#include "minibidi.h"
}

#undef when
#undef otherwise

#include <Logging.h>
#include <Utf8.h>

#include <cstring>
#include <mutex>

// Guards the static bidi_char buffers in applyBidiVisual() and
// computeVisualWordOrder().  The bidi+shaping pipeline is not reentrant;
// this mutex serialises access so multi-core callers don't corrupt each
// other's intermediate state.
static std::mutex bidiMutex;

namespace {

bool isNaturalDirectionClass(const uchar cls) {
  switch (cls) {
    case L:
    case R:
    case AL:
    case EN:
    case AN:
      return true;
    default:
      return false;
  }
}

// Both public bidi paths are serialized by bidiMutex and never call each
// other, so one line buffer is sufficient. Keeping the buffer shared avoids
// retaining two copies of the same bounded X3 scratch allocation.
bidi_char sharedBidiLine[BIDI_MAX_LINE];
bidi_char sharedBidiShaped[BIDI_MAX_LINE];

bool isSegmentBreak(const uint32_t cp) {
  return cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0x2028 || cp == 0x2029;
}

const unsigned char* findSegmentEnd(const unsigned char* start) {
  const unsigned char* p = start;
  const unsigned char* lastBreak = nullptr;
  int count = 0;
  while (*p && count < BIDI_MAX_LINE) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    ++count;
    if (isSegmentBreak(cp)) lastBreak = p;
  }
  if (*p && lastBreak && lastBreak > start) return lastBreak;
  return p;
}

size_t countWordCodepoints(const std::string& word) {
  size_t count = 0;
  auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
  while (*p && count <= BIDI_MAX_LINE) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    ++count;
  }
  return count;
}

size_t findWordChunkEnd(const std::vector<std::string>& words, const size_t start) {
  size_t codepoints = 0;
  size_t end = start;
  while (end < words.size()) {
    const size_t wordCodepoints = countWordCodepoints(words[end]);
    const size_t separator = end == start ? 0 : 1;
    if (end > start && codepoints + separator + wordCodepoints > BIDI_MAX_LINE) break;
    ++end;
    codepoints += separator + wordCodepoints;
    if (wordCodepoints > BIDI_MAX_LINE) break;
  }
  return end;
}

void inspectWordDirection(const std::string& word, bool& hasRtl, bool& hasLtr) {
  auto* p = reinterpret_cast<const unsigned char*>(word.c_str());
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    const Utf8Script script = utf8ClassifyScript(cp);
    const uchar bidiClass = bidi_class(cp);
    hasRtl = hasRtl || script == Utf8Script::Arabic || script == Utf8Script::Hebrew || bidiClass == R || bidiClass == AL;
    hasLtr = hasLtr || script == Utf8Script::Latin || script == Utf8Script::Cjk || bidiClass == L || bidiClass == EN;
  }
}

}  // namespace

namespace BidiUtils {

bool startsWithRtl(const char* utf8, int maxStrongChars) {
  if (!utf8 || maxStrongChars <= 0) return false;

  auto* p = reinterpret_cast<const unsigned char*>(utf8);
  int checked = 0;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;

    const Utf8Script script = utf8ClassifyScript(cp);
    if (script == Utf8Script::Arabic || script == Utf8Script::Hebrew) return true;
    const uchar cls = bidi_class(cp);
    if (cls == R || cls == AL) return true;
    if (cls == L) return false;
    checked++;
    if (checked >= maxStrongChars) break;
  }
  return false;
}

int detectParagraphLevel(const char* utf8, const int fallbackLevel, const int maxStrongChars) {
  if (!utf8 || maxStrongChars <= 0) return fallbackLevel & 1;

  auto* p = reinterpret_cast<const unsigned char*>(utf8);
  int checked = 0;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;

    const Utf8Script script = utf8ClassifyScript(cp);
    if (script == Utf8Script::Arabic || script == Utf8Script::Hebrew) return 1;
    const uchar cls = bidi_class(cp);
    if (cls == R || cls == AL) return 1;
    if (cls == L) return 0;
    checked++;
    if (checked >= maxStrongChars) break;
  }

  return fallbackLevel & 1;
}

bool isTransparentMark(const uint32_t cp) {
  return utf8IsTransparentMark(cp);
}

bool applyBidiVisualChunk(const char* utf8, std::string& out, int paragraphLevel) {
  if (!utf8 || !*utf8) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);

  bidi_char* const line = sharedBidiLine;
  bidi_char* const shaped = sharedBidiShaped;
  int count = 0;
  int lastBase = -1;           // last non-formatter character (mintty's ibase)
  uint8_t pendingJoiners = 0;  // ZWJ/ZWNJ seen since lastBase
  auto* p = reinterpret_cast<const unsigned char*>(utf8);
  while (*p) {
    if (count >= BIDI_MAX_LINE) {
      LOG_DBG("BIDI", "applyBidiVisual: input exceeds BIDI_MAX_LINE (%d chars), returning unprocessed", BIDI_MAX_LINE);
      return false;
    }

    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    line[count].origwc = line[count].wc = cp;
    line[count].index = static_cast<uint16_t>(count);
    line[count].joiners = 0;

    // Flag Arabic joining formatters mintty-style (termline.c): the ZWJ/ZWNJ
    // goes into the low nibble of the character it follows and the high
    // nibble of the character it precedes.  Flags are assigned in logical
    // order here; do_shape() reads them after reordering.
    if (cp == 0x200C || cp == 0x200D) {
      const uint8_t joiner = (cp == 0x200D) ? ZWJ : ZWNJ;
      if (lastBase >= 0) line[lastBase].joiners |= joiner;
      pendingJoiners |= joiner;
    } else {
      line[count].joiners = pendingJoiners << 4;
      pendingJoiners = 0;
      lastBase = count;
    }
    count++;
  }
  if (!count) return false;

  const bool autodir = (paragraphLevel < 0);
  const int level = autodir ? 0 : (paragraphLevel & 1);

  // Order matters (mintty does the same): do_bidi() first to obtain visual
  // order, then do_shape() — contextual forms are resolved from *visual*
  // adjacency, and shaping presentation forms must never be reordered.
  do_bidi(autodir, level, line, count);
  do_shape(line, shaped, count);

  out.clear();
  out.reserve(std::strlen(utf8));
  // Lam-Alef collapse sentinel and zero-width joining formatters have done
  // their job during shaping and have no glyphs to render.
  const auto filtered = [](const uint32_t cp) { return cp == LIGATURE_PLACEHOLDER || utf8IsNonRenderingFormat(cp); };
  for (int i = 0; i < count; i++) {
    const uint32_t cp = shaped[i].wc;
    if (filtered(cp)) continue;
    if (!isTransparentMark(cp)) {
      utf8AppendCodepoint(cp, out);
      continue;
    }
    // UAX#9 rule L3: reversing an RTL run leaves combining marks *before*
    // their base character. The renderer overlays a mark on the most
    // recently drawn glyph, so emit the base first, then its marks in
    // logical order. `index` is the original logical position: a base
    // following its marks with a *lower* index means the run was reversed.
    int j = i;  // [i, j) = the run of marks (and filtered entries)
    while (j < count && (filtered(shaped[j].wc) || isTransparentMark(shaped[j].wc))) j++;
    if (j < count && shaped[j].index < shaped[i].index) {
      utf8AppendCodepoint(shaped[j].wc, out);
      for (int k = j - 1; k >= i; k--) {
        if (isTransparentMark(shaped[k].wc)) utf8AppendCodepoint(shaped[k].wc, out);
      }
      i = j;  // base already emitted
    } else {
      // Unreversed (or trailing, base-less) marks already follow their base.
      for (int k = i; k < j; k++) {
        if (isTransparentMark(shaped[k].wc)) utf8AppendCodepoint(shaped[k].wc, out);
      }
      i = j - 1;
    }
  }
  return true;
}

bool applyBidiVisual(const char* utf8, std::string& out, int paragraphLevel) {
  if (!utf8 || !*utf8) return false;

  const auto* begin = reinterpret_cast<const unsigned char*>(utf8);
  const auto* firstEnd = findSegmentEnd(begin);
  if (!firstEnd || !*firstEnd) {
    return applyBidiVisualChunk(utf8, out, paragraphLevel);
  }

  // Long lines are processed as bounded codepoint segments. For an RTL
  // paragraph, rescan the already-bounded segment boundaries in reverse so
  // this fallback stays visual-order oriented without storing a whole line.
  const int level = paragraphLevel < 0 ? detectParagraphLevel(utf8, 0, 64) : (paragraphLevel & 1);
  int segmentCount = 0;
  for (const auto* p = begin; *p;) {
    const auto* end = findSegmentEnd(p);
    if (end <= p) break;
    ++segmentCount;
    p = end;
  }
  if (segmentCount <= 0) return false;

  out.clear();
  out.reserve(std::strlen(utf8));
  const auto processSegment = [&](const unsigned char* start, const unsigned char* end) {
    std::string chunk(reinterpret_cast<const char*>(start), static_cast<size_t>(end - start));
    std::string visual;
    if (!applyBidiVisualChunk(chunk.c_str(), visual, level)) return false;
    out += visual;
    return true;
  };

  if (level == 0) {
    const auto* p = begin;
    while (*p) {
      const auto* end = findSegmentEnd(p);
      if (end <= p || !processSegment(p, end)) return false;
      p = end;
    }
  } else {
    for (int target = segmentCount - 1; target >= 0; --target) {
      const auto* p = begin;
      for (int index = 0; index < target; ++index) {
        const auto* end = findSegmentEnd(p);
        if (end <= p) return false;
        p = end;
      }
      const auto* end = findSegmentEnd(p);
      if (end <= p || !processSegment(p, end)) return false;
    }
  }
  LOG_DBG("BIDI", "applyBidiVisual: segmented input into %d bounded runs", segmentCount);
  return !out.empty();
}

bool computeVisualWordOrderSegmented(const std::vector<std::string>& words, const bool paragraphIsRtl,
                                     std::vector<uint16_t>& visualOrder) {
  if (words.size() > UINT16_MAX) return false;

  bool hasRtl = false;
  bool hasLtr = false;
  for (const auto& word : words) inspectWordDirection(word, hasRtl, hasLtr);
  if (!hasRtl) {
    if (paragraphIsRtl) {
      visualOrder.reserve(words.size());
      for (size_t i = 0; i < words.size(); ++i) visualOrder.push_back(static_cast<uint16_t>(i));
      return true;
    }
    return false;
  }
  if (paragraphIsRtl && !hasLtr) {
    visualOrder.reserve(words.size());
    for (int i = static_cast<int>(words.size()) - 1; i >= 0; --i) {
      visualOrder.push_back(static_cast<uint16_t>(i));
    }
    return true;
  }

  int segmentCount = 0;
  for (size_t start = 0; start < words.size();) {
    const size_t end = findWordChunkEnd(words, start);
    if (end <= start) return false;
    ++segmentCount;
    start = end;
  }
  if (segmentCount == 0) return false;

  bool anyReordered = false;
  const auto processSegment = [&](const size_t start, const size_t end) {
    std::vector<std::string> chunk;
    chunk.reserve(end - start);
    for (size_t i = start; i < end; ++i) chunk.push_back(words[i]);

    std::vector<uint16_t> localOrder;
    const bool reordered = computeVisualWordOrder(chunk, paragraphIsRtl, localOrder);
    anyReordered = anyReordered || reordered;
    if (reordered && localOrder.size() == chunk.size()) {
      for (const uint16_t localIndex : localOrder) {
        visualOrder.push_back(static_cast<uint16_t>(start + localIndex));
      }
    } else {
      for (size_t i = start; i < end; ++i) visualOrder.push_back(static_cast<uint16_t>(i));
    }
    return true;
  };

  if (!paragraphIsRtl) {
    for (size_t start = 0; start < words.size();) {
      const size_t end = findWordChunkEnd(words, start);
      if (!processSegment(start, end)) return false;
      start = end;
    }
  } else {
    for (int target = segmentCount - 1; target >= 0; --target) {
      size_t start = 0;
      size_t end = 0;
      for (int index = 0; index <= target; ++index) {
        start = end;
        end = findWordChunkEnd(words, start);
        if (end <= start) return false;
      }
      if (!processSegment(start, end)) return false;
    }
  }

  LOG_DBG("BIDI", "computeVisualWordOrder: segmented %zu words into %d bounded runs", words.size(), segmentCount);
  return paragraphIsRtl || anyReordered;
}

bool computeVisualWordOrder(const std::vector<std::string>& words, bool paragraphIsRtl,
                            std::vector<uint16_t>& visualOrder) {
  visualOrder.clear();
  const size_t nWords = words.size();
  if (nWords <= 1) return false;
  if (nWords > BIDI_MAX_LINE) return computeVisualWordOrderSegmented(words, paragraphIsRtl, visualOrder);
  if (nWords > UINT16_MAX) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);

  bidi_char* const line = sharedBidiLine;
  int count = 0;
  bool truncated = false;

  for (size_t w = 0; w < nWords && !truncated; w++) {
    auto* p = reinterpret_cast<const unsigned char*>(words[w].c_str());
    while (*p) {
      if (count >= BIDI_MAX_LINE) {
        truncated = true;
        break;
      }
      const uint32_t cp = utf8NextCodepoint(&p);
      if (!cp || cp == REPLACEMENT_GLYPH) break;
      line[count].origwc = line[count].wc = cp;
      line[count].index = static_cast<uint16_t>(w);
      line[count].joiners = 0;
      count++;
    }

    if (!truncated && w + 1 < nWords) {
      if (count >= BIDI_MAX_LINE) {
        truncated = true;
        break;
      }
      line[count].origwc = line[count].wc = ' ';
      line[count].index = static_cast<uint16_t>(nWords);
      line[count].joiners = 0;
      count++;
    }
  }

  if (truncated || count == 0) return false;

  // Fast-path for homogeneous lines: skip UAX#9 if there's no mixing.
  bool hasL = false, hasR = false;
  for (int i = 0; i < count; i++) {
    uchar bc = bidi_class(line[i].wc);
    if (bc == L || bc == EN || bc == AN)
      hasL = true;
    else if (bc == R || bc == AL)
      hasR = true;
  }

  // Purely LTR line in RTL paragraph: identity order, but we might still need to reorder
  // if some characters are mirrored or neutral resolution differs.
  // Actually, UAX#9 rule L1/L2 says purely LTR in RTL para stays as is (identity).
  // Purely RTL line: just reverse the words.
  if (!hasL && hasR && paragraphIsRtl) {
    visualOrder.reserve(nWords);
    for (int i = static_cast<int>(nWords) - 1; i >= 0; i--) {
      visualOrder.push_back(static_cast<uint16_t>(i));
    }
    return true;
  }
  if (!hasR) {
    if (!paragraphIsRtl) {
      // Pure LTR in LTR paragraph: nothing to do.
      return false;
    }
    // Pure LTR in RTL paragraph: no word reordering, but must use the
    // willReorder (left-to-right) positioning path, not the RTL right-to-left path.
    visualOrder.reserve(nWords);
    for (size_t i = 0; i < nWords; i++) {
      visualOrder.push_back(static_cast<uint16_t>(i));
    }
    return true;
  }

  do_bidi(/*autodir=*/false, paragraphIsRtl ? 1 : 0, line, count);

  uint16_t firstAny[BIDI_MAX_LINE];
  uint16_t firstNatural[BIDI_MAX_LINE];
  for (size_t w = 0; w < nWords; w++) {
    firstAny[w] = UINT16_MAX;
    firstNatural[w] = UINT16_MAX;
  }

  for (int i = 0; i < count; i++) {
    const uint16_t w = line[i].index;
    if (w >= nWords) continue;

    if (firstAny[w] == UINT16_MAX) {
      firstAny[w] = static_cast<uint16_t>(i);
    }

    if (firstNatural[w] == UINT16_MAX && isNaturalDirectionClass(bidi_class(line[i].wc))) {
      firstNatural[w] = static_cast<uint16_t>(i);
    }
  }

  visualOrder.reserve(nWords);
  for (int i = 0; i < count; i++) {
    const uint16_t w = line[i].index;
    if (w >= nWords) continue;

    const uint16_t anchor = firstNatural[w] != UINT16_MAX ? firstNatural[w] : firstAny[w];
    if (anchor == UINT16_MAX) {
      visualOrder.clear();
      return false;
    }
    if (anchor == static_cast<uint16_t>(i)) {
      visualOrder.push_back(w);
    }
  }

  if (visualOrder.size() != nWords) {
    visualOrder.clear();
    return false;
  }

  // Check if the order is exactly the same as the original input
  bool needsReorder = false;
  for (size_t i = 0; i < nWords; i++) {
    if (visualOrder[i] != i) {
      needsReorder = true;
      break;
    }
  }

  if (!needsReorder) {
    visualOrder.clear();
    return false;
  }

  return true;
}

}  // namespace BidiUtils
