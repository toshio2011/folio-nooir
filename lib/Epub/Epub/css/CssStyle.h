#pragma once

#include <cstdint>

// Matches order of PARAGRAPH_ALIGNMENT in CrossPointSettings
enum class CssTextAlign : uint8_t { Justify = 0, Left = 1, Center = 2, Right = 3, None = 4 };
enum class CssUnit : uint8_t { Pixels = 0, Em = 1, Rem = 2, Points = 3, Percent = 4, Unitless = 5 };
enum class CssTextDirection : uint8_t { Ltr = 0, Rtl = 1 };

// Represents a CSS length value with its unit, allowing deferred resolution to pixels
struct CssLength {
  float value = 0.0f;
  CssUnit unit = CssUnit::Pixels;

  CssLength() = default;
  CssLength(const float v, const CssUnit u) : value(v), unit(u) {}

  // Convenience constructor for pixel values (most common case)
  explicit CssLength(const float pixels) : value(pixels) {}

  // Returns true if this length can be resolved to pixels with the given context.
  // Percentage units require a non-zero containerWidth to resolve.
  [[nodiscard]] bool isResolvable(const float containerWidth = 0) const {
    return unit != CssUnit::Percent || containerWidth > 0;
  }

  // Resolve to pixels given the current em size (font line height)
  // containerWidth is needed for percentage units (e.g. viewport width)
  [[nodiscard]] float toPixels(const float emSize, const float containerWidth = 0) const {
    switch (unit) {
      case CssUnit::Em:
      case CssUnit::Rem:
        return value * emSize;
      case CssUnit::Points:
        return value * 1.33f;  // Approximate pt to px conversion
      case CssUnit::Percent:
        return value * containerWidth / 100.0f;
      case CssUnit::Unitless:
        return value;
      default:
        return value;
    }
  }

  // Resolve to int16_t pixels (for BlockStyle fields)
  [[nodiscard]] int16_t toPixelsInt16(const float emSize, const float containerWidth = 0) const {
    return static_cast<int16_t>(toPixels(emSize, containerWidth));
  }
};

// Font style options matching CSS font-style property
enum class CssFontStyle : uint8_t { Normal = 0, Italic = 1 };

// Font weight options - CSS supports 100-900, we simplify to normal/bold
enum class CssFontWeight : uint8_t { Normal = 0, Bold = 1 };

// Text decoration options. Values are bit flags so CSS can combine multiple line decorations.
enum class CssTextDecoration : uint8_t { None = 0, Underline = 1, LineThrough = 2 };

constexpr CssTextDecoration operator|(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr CssTextDecoration operator&(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr uint8_t CSS_TEXT_DECORATION_MASK =
    static_cast<uint8_t>(CssTextDecoration::Underline) | static_cast<uint8_t>(CssTextDecoration::LineThrough);

// Display options - only None and Block are relevant for e-ink rendering
enum class CssDisplay : uint8_t { Block = 0, None = 1 };

// Vertical alignment options for inline elements (e.g. superscript/subscript)
enum class CssVerticalAlign : uint8_t { Baseline = 0, Super = 1, Sub = 2 };

// Bitmask for tracking which properties have been explicitly set
struct CssPropertyFlags {
  uint16_t textAlign : 1;
  uint16_t fontStyle : 1;
  uint16_t fontWeight : 1;
  uint16_t textDecoration : 1;
  uint16_t textIndent : 1;
  uint16_t marginTop : 1;
  uint16_t marginBottom : 1;
  uint16_t marginLeft : 1;
  uint16_t marginRight : 1;
  uint16_t paddingTop : 1;
  uint16_t paddingBottom : 1;
  uint16_t paddingLeft : 1;
  uint16_t paddingRight : 1;
  uint16_t imageHeight : 1;
  uint16_t imageWidth : 1;
  uint16_t display : 1;
  uint16_t direction : 1;
  uint16_t verticalAlign : 1;
  uint16_t fontSize : 1;
  uint16_t lineHeight : 1;

  CssPropertyFlags()
      : textAlign(0),
        fontStyle(0),
        fontWeight(0),
        textDecoration(0),
        textIndent(0),
        marginTop(0),
        marginBottom(0),
        marginLeft(0),
        marginRight(0),
        paddingTop(0),
        paddingBottom(0),
        paddingLeft(0),
        paddingRight(0),
        imageHeight(0),
        imageWidth(0),
        display(0),
        direction(0),
        verticalAlign(0),
        fontSize(0),
        lineHeight(0) {}

  [[nodiscard]] bool anySet() const {
    return textAlign || fontStyle || fontWeight || textDecoration || textIndent || marginTop || marginBottom ||
           marginLeft || marginRight || paddingTop || paddingBottom || paddingLeft || paddingRight || imageHeight ||
           imageWidth || display || direction || verticalAlign || fontSize || lineHeight;
  }

  void clearAll() {
    textAlign = fontStyle = fontWeight = textDecoration = textIndent = 0;
    marginTop = marginBottom = marginLeft = marginRight = 0;
    paddingTop = paddingBottom = paddingLeft = paddingRight = 0;
    imageHeight = imageWidth = display = direction = verticalAlign = fontSize = lineHeight = 0;
  }
};

// Cache serializes defined flags and per-property importance as uint32_t masks
// with bit indices 0..19.
static_assert(sizeof(CssPropertyFlags) <= sizeof(uint32_t),
              "CssPropertyFlags exceeds 32 bits; update cache read/write in CssParser.cpp");

// Stable property slots shared by cascade priority tracking and CSS-cache
// serialization. Keep this order in lockstep with CssPropertyFlags and the
// definedBits/importantBits cache fields.
struct CssPropertyIndex {
  static constexpr uint8_t TextAlign = 0;
  static constexpr uint8_t FontStyle = 1;
  static constexpr uint8_t FontWeight = 2;
  static constexpr uint8_t TextDecoration = 3;
  static constexpr uint8_t TextIndent = 4;
  static constexpr uint8_t MarginTop = 5;
  static constexpr uint8_t MarginBottom = 6;
  static constexpr uint8_t MarginLeft = 7;
  static constexpr uint8_t MarginRight = 8;
  static constexpr uint8_t PaddingTop = 9;
  static constexpr uint8_t PaddingBottom = 10;
  static constexpr uint8_t PaddingLeft = 11;
  static constexpr uint8_t PaddingRight = 12;
  static constexpr uint8_t ImageHeight = 13;
  static constexpr uint8_t ImageWidth = 14;
  static constexpr uint8_t Display = 15;
  static constexpr uint8_t Direction = 16;
  static constexpr uint8_t VerticalAlign = 17;
  static constexpr uint8_t FontSize = 18;
  static constexpr uint8_t LineHeight = 19;
  static constexpr uint8_t Count = 20;
};

// Represents a collection of CSS style properties
// Only stores properties relevant to e-ink text rendering
// Length values are stored as CssLength (value + unit) for deferred resolution
struct CssStyle {
  CssTextAlign textAlign = CssTextAlign::Left;
  CssFontStyle fontStyle = CssFontStyle::Normal;
  CssFontWeight fontWeight = CssFontWeight::Normal;
  CssTextDecoration textDecoration = CssTextDecoration::None;
  CssTextDirection direction = CssTextDirection::Ltr;

  CssLength textIndent;     // First-line indent (deferred resolution)
  CssLength marginTop;      // Vertical spacing before block
  CssLength marginBottom;   // Vertical spacing after block
  CssLength marginLeft;     // Horizontal spacing left of block
  CssLength marginRight;    // Horizontal spacing right of block
  CssLength paddingTop;     // Padding before
  CssLength paddingBottom;  // Padding after
  CssLength paddingLeft;    // Padding left
  CssLength paddingRight;   // Padding right
  CssLength imageHeight;    // Height for img (e.g. 2em) – width derived from aspect ratio when only height set
  CssLength imageWidth;     // Width for img when both or only width set
  CssLength fontSize;       // Publisher font size (bounded at layout time)
  CssLength lineHeight;     // Publisher line height (unitless/length, bounded at layout time)
  CssDisplay display = CssDisplay::Block;                       // display property (Block or None)
  CssVerticalAlign verticalAlign = CssVerticalAlign::Baseline;  // vertical-align (super/sub positioning)

  CssPropertyFlags defined;  // Tracks which properties were explicitly set
  uint32_t importantBits = 0;  // Per-property !important flags, slots 0..19

  [[nodiscard]] bool isImportant(const uint8_t property) const {
    return property < CssPropertyIndex::Count && (importantBits & (uint32_t{1} << property)) != 0;
  }

  void setImportant(const uint8_t property, const bool important) {
    if (property >= CssPropertyIndex::Count) return;
    const uint32_t mask = uint32_t{1} << property;
    if (important) {
      importantBits |= mask;
    } else {
      importantBits &= ~mask;
    }
  }

  // Apply properties from another style, only overwriting if the other style
  // has that property explicitly defined. This is also used to apply inline
  // declarations after stylesheet resolution: an inline important declaration
  // wins over stylesheet importance, while stylesheet importance defeats an
  // ordinary inline declaration.
  void applyOver(const CssStyle& base) {
    const auto shouldApply = [&](const uint8_t property, const bool definedHere) {
      return definedHere && (base.isImportant(property) || !isImportant(property));
    };
    const auto recordImportance = [&](const uint8_t property) { setImportant(property, base.isImportant(property)); };

    if (shouldApply(CssPropertyIndex::TextAlign, base.hasTextAlign())) {
      textAlign = base.textAlign;
      defined.textAlign = 1;
      recordImportance(CssPropertyIndex::TextAlign);
    }
    if (shouldApply(CssPropertyIndex::FontStyle, base.hasFontStyle())) {
      fontStyle = base.fontStyle;
      defined.fontStyle = 1;
      recordImportance(CssPropertyIndex::FontStyle);
    }
    if (shouldApply(CssPropertyIndex::FontWeight, base.hasFontWeight())) {
      fontWeight = base.fontWeight;
      defined.fontWeight = 1;
      recordImportance(CssPropertyIndex::FontWeight);
    }
    if (shouldApply(CssPropertyIndex::TextDecoration, base.hasTextDecoration())) {
      textDecoration = base.textDecoration;
      defined.textDecoration = 1;
      recordImportance(CssPropertyIndex::TextDecoration);
    }
    if (shouldApply(CssPropertyIndex::TextIndent, base.hasTextIndent())) {
      textIndent = base.textIndent;
      defined.textIndent = 1;
      recordImportance(CssPropertyIndex::TextIndent);
    }
    if (shouldApply(CssPropertyIndex::MarginTop, base.hasMarginTop())) {
      marginTop = base.marginTop;
      defined.marginTop = 1;
      recordImportance(CssPropertyIndex::MarginTop);
    }
    if (shouldApply(CssPropertyIndex::MarginBottom, base.hasMarginBottom())) {
      marginBottom = base.marginBottom;
      defined.marginBottom = 1;
      recordImportance(CssPropertyIndex::MarginBottom);
    }
    if (shouldApply(CssPropertyIndex::MarginLeft, base.hasMarginLeft())) {
      marginLeft = base.marginLeft;
      defined.marginLeft = 1;
      recordImportance(CssPropertyIndex::MarginLeft);
    }
    if (shouldApply(CssPropertyIndex::MarginRight, base.hasMarginRight())) {
      marginRight = base.marginRight;
      defined.marginRight = 1;
      recordImportance(CssPropertyIndex::MarginRight);
    }
    if (shouldApply(CssPropertyIndex::PaddingTop, base.hasPaddingTop())) {
      paddingTop = base.paddingTop;
      defined.paddingTop = 1;
      recordImportance(CssPropertyIndex::PaddingTop);
    }
    if (shouldApply(CssPropertyIndex::PaddingBottom, base.hasPaddingBottom())) {
      paddingBottom = base.paddingBottom;
      defined.paddingBottom = 1;
      recordImportance(CssPropertyIndex::PaddingBottom);
    }
    if (shouldApply(CssPropertyIndex::PaddingLeft, base.hasPaddingLeft())) {
      paddingLeft = base.paddingLeft;
      defined.paddingLeft = 1;
      recordImportance(CssPropertyIndex::PaddingLeft);
    }
    if (shouldApply(CssPropertyIndex::PaddingRight, base.hasPaddingRight())) {
      paddingRight = base.paddingRight;
      defined.paddingRight = 1;
      recordImportance(CssPropertyIndex::PaddingRight);
    }
    if (shouldApply(CssPropertyIndex::ImageHeight, base.hasImageHeight())) {
      imageHeight = base.imageHeight;
      defined.imageHeight = 1;
      recordImportance(CssPropertyIndex::ImageHeight);
    }
    if (shouldApply(CssPropertyIndex::ImageWidth, base.hasImageWidth())) {
      imageWidth = base.imageWidth;
      defined.imageWidth = 1;
      recordImportance(CssPropertyIndex::ImageWidth);
    }
    if (shouldApply(CssPropertyIndex::Display, base.hasDisplay())) {
      display = base.display;
      defined.display = 1;
      recordImportance(CssPropertyIndex::Display);
    }
    if (shouldApply(CssPropertyIndex::Direction, base.hasDirection())) {
      direction = base.direction;
      defined.direction = 1;
      recordImportance(CssPropertyIndex::Direction);
    }
    if (shouldApply(CssPropertyIndex::VerticalAlign, base.hasVerticalAlign())) {
      verticalAlign = base.verticalAlign;
      defined.verticalAlign = 1;
      recordImportance(CssPropertyIndex::VerticalAlign);
    }
    if (shouldApply(CssPropertyIndex::FontSize, base.hasFontSize())) {
      fontSize = base.fontSize;
      defined.fontSize = 1;
      recordImportance(CssPropertyIndex::FontSize);
    }
    if (shouldApply(CssPropertyIndex::LineHeight, base.hasLineHeight())) {
      lineHeight = base.lineHeight;
      defined.lineHeight = 1;
      recordImportance(CssPropertyIndex::LineHeight);
    }
  }

  [[nodiscard]] bool hasTextAlign() const { return defined.textAlign; }
  [[nodiscard]] bool hasFontStyle() const { return defined.fontStyle; }
  [[nodiscard]] bool hasFontWeight() const { return defined.fontWeight; }
  [[nodiscard]] bool hasTextDecoration() const { return defined.textDecoration; }
  [[nodiscard]] bool hasTextIndent() const { return defined.textIndent; }
  [[nodiscard]] bool hasMarginTop() const { return defined.marginTop; }
  [[nodiscard]] bool hasMarginBottom() const { return defined.marginBottom; }
  [[nodiscard]] bool hasMarginLeft() const { return defined.marginLeft; }
  [[nodiscard]] bool hasMarginRight() const { return defined.marginRight; }
  [[nodiscard]] bool hasPaddingTop() const { return defined.paddingTop; }
  [[nodiscard]] bool hasPaddingBottom() const { return defined.paddingBottom; }
  [[nodiscard]] bool hasPaddingLeft() const { return defined.paddingLeft; }
  [[nodiscard]] bool hasPaddingRight() const { return defined.paddingRight; }
  [[nodiscard]] bool hasImageHeight() const { return defined.imageHeight; }
  [[nodiscard]] bool hasImageWidth() const { return defined.imageWidth; }
  [[nodiscard]] bool hasDisplay() const { return defined.display; }
  [[nodiscard]] bool hasDirection() const { return defined.direction; }
  [[nodiscard]] bool hasVerticalAlign() const { return defined.verticalAlign; }
  [[nodiscard]] bool hasFontSize() const { return defined.fontSize; }
  [[nodiscard]] bool hasLineHeight() const { return defined.lineHeight; }

  void reset() {
    textAlign = CssTextAlign::Left;
    fontStyle = CssFontStyle::Normal;
    fontWeight = CssFontWeight::Normal;
    textDecoration = CssTextDecoration::None;
    direction = CssTextDirection::Ltr;
    textIndent = CssLength{};
    marginTop = marginBottom = marginLeft = marginRight = CssLength{};
    paddingTop = paddingBottom = paddingLeft = paddingRight = CssLength{};
    imageHeight = imageWidth = CssLength{};
    fontSize = lineHeight = CssLength{};
    display = CssDisplay::Block;
    verticalAlign = CssVerticalAlign::Baseline;
    defined.clearAll();
    importantBits = 0;
  }
};
