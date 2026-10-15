#include "EpdFontFamily.h"

const EpdFont* EpdFontFamily::getFont(const Style style) const {
  // Extract font style bits; render-time overlay bits do not affect font selection.
  const bool hasBold = (style & BOLD) != 0;
  const bool hasItalic = (style & ITALIC) != 0;

  if (hasBold && hasItalic) {
    if (boldItalic) return boldItalic;
    if (bold) return bold;
    if (italic) return italic;
  } else if (hasBold && bold) {
    return bold;
  } else if (hasItalic && italic) {
    return italic;
  }

  return regular;
}

EpdFontFamily EpdFontFamily::withStyleFallbacks(const EpdFontFamily& primary, const EpdFontFamily& fallback,
                                                const uint8_t primaryStyleMask) {
  const auto choose = [primaryStyleMask](const uint8_t style, const EpdFont* primaryFont,
                                         const EpdFont* fallbackFont) {
    if ((primaryStyleMask & (1u << style)) != 0 && primaryFont != nullptr) return primaryFont;
    return fallbackFont != nullptr ? fallbackFont : primaryFont;
  };
  return EpdFontFamily(choose(REGULAR, primary.regular, fallback.regular),
                       choose(BOLD, primary.bold, fallback.bold),
                       choose(ITALIC, primary.italic, fallback.italic),
                       choose(BOLD_ITALIC, primary.boldItalic, fallback.boldItalic));
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  getFont(style)->getTextDimensions(string, w, h);
}

const EpdFontData* EpdFontFamily::getData(const Style style) const { return getFont(style)->data; }

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  return getFont(style)->getGlyph(cp);
}

bool EpdFontFamily::hasCodepoint(const uint32_t cp, const Style style) const {
  return getFont(style)->hasCodepoint(cp);
}

int8_t EpdFontFamily::getKerning(const uint32_t leftCp, const uint32_t rightCp, const Style style) const {
  return getFont(style)->getKerning(leftCp, rightCp);
}

uint32_t EpdFontFamily::applyLigatures(const uint32_t cp, const char*& text, const Style style) const {
  return getFont(style)->applyLigatures(cp, text);
}
