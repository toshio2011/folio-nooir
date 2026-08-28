#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <array>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"
#include "Epub/parsers/TocNavAmpersandSanitizer.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool forceParagraphIndents;
  const CssParser* cssParser;
  bool embeddedStyle;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
    bool hasRedaction = false, redaction = false;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  bool blockBoundaryPending = false;       // a closed block must end before the next content

  // Typography is resolved while the incremental parser visits elements.  The
  // stack is deliberately capped: deeply nested decorative markup must not be
  // able to grow a second unbounded style structure on the X3.
  struct TypographyState {
    int depth = 0;
    float fontSizePt = 14.0f;
    uint8_t fontPointSize = 0;
    float lineHeightMultiplier = 0.0f;
    uint16_t lineHeightPx = 0;
  };
  static constexpr size_t MAX_TYPOGRAPHY_DEPTH = 32;
  std::vector<TypographyState> typographyStack;

  CssStyle currentCssStyle;
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveSup = false;
  bool effectiveSub = false;
  bool effectiveRedaction = false;
  bool redactionWhitespaceAdded = false;
  int tableDepth = 0;
  int tableRowIndex = 0;
  int tableColIndex = 0;
  uint8_t listDepth = 0;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet
  // Allocation failures are reported back through parseStep() instead of
  // dereferencing a null page/block or continuing with a corrupt partial.
  bool allocationFailed_ = false;

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on
  int completedPageCount = 0;
  std::vector<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;          // deferred until after previous text block is flushed
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  std::vector<std::string> referencedAnchorIds;  // bounded same-file fragment references seen so far
  std::vector<std::pair<std::string, uint16_t>> unresolvedInlineAnchors;
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  // Resumable parse state. The one-shot parseAndBuildPages() drives these
  // internally; the incremental section builder drives them across render ticks
  // so a large single chapter can yield between pages instead of blocking the UI
  // until the whole thing is laid out. parseFile_ and the expat parser stay alive
  // for the lifetime of the parse so it can be paused and resumed at buffer
  // boundaries.
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;
  size_t parserBytesAtLastYield_ = 0;
  bool documentRootSeen_ = false;
  bool documentRootClosed_ = false;
  static constexpr size_t PARSE_BUFFER_SIZE = 1024;
  std::unique_ptr<TocNavAmpersandSanitizer> ampersandSanitizer_;
  std::unique_ptr<std::array<uint8_t, PARSE_BUFFER_SIZE>> recoveryInputBuffer_;
  bool recoverBareAmpersands_ = false;
  XML_Error parseError_ = XML_ERROR_NONE;

  static bool writeSanitizedChunk(void* userData, const uint8_t* data, size_t size);

  void rememberReferencedAnchor(const char* href);
  void rememberUnresolvedInlineAnchor(const char* id);
  bool isReferencedAnchor(const char* id) const;
  void updateEffectiveInlineStyle();
  void failAllocation(const char* what);
  void startNewTextBlock(const BlockStyle& blockStyle);
  void consumePendingBlockBoundary();
  void flushPendingAnchor();
  void flushPartWordBuffer();
  void makePages();
  TypographyState makeTypographyState(const CssStyle& cssStyle) const;
  void pushTypographyState(int elementDepth, const CssStyle& cssStyle);
  void popTypographyState(int elementDepth);
  const TypographyState& currentTypographyState() const;
  void applyTypographyToBlockStyle(BlockStyle& blockStyle, const TypographyState& typography) const;
  int lineHeightForBlock(const BlockStyle& blockStyle) const;
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  explicit ChapterHtmlSlimParser(std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer,
                                 const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                 const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                 const uint16_t viewportHeight, const bool hyphenationEnabled,
                                 const bool focusReadingEnabled, const bool forceParagraphIndents,
                                 const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t)>& completePageFn,
                                 const bool embeddedStyle, const std::string& contentBase,
                                 const std::string& imageBasePath, const uint8_t imageRendering = 0,
                                 std::vector<std::string> tocAnchors = {},
                                 const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr,
                                 const bool recoverBareAmpersands = false)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(fontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        forceParagraphIndents(forceParagraphIndents),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(cssParser),
        embeddedStyle(embeddedStyle),
        imageRendering(imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)),
        recoverBareAmpersands_(recoverBareAmpersands) {}

  ~ChapterHtmlSlimParser();

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   loop: switch (parseStep()) { More: keep going / yield; Done: finishParse(); Error: abortParse(); }
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; returns true
  void abortParse();   // tear down without flushing (error / abandon)
  bool shouldRetryWithAmpersandRecovery() const {
    return !recoverBareAmpersands_ && parseError_ == XML_ERROR_INVALID_TOKEN;
  }

  void addLineToPage(std::shared_ptr<TextBlock> line);
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
