#pragma once
#include <Print.h>
#include <expat.h>

#include <string>

class BookMetadataCache;

// Parser for EPUB 3 nav.xhtml navigation documents
// Parses HTML5 nav elements with epub:type="toc" to extract table of contents
class TocNavParser final : public Print {
  enum ParserState {
    START,
    IN_HTML,
    IN_BODY,
    IN_NAV_TOC,  // Inside <nav epub:type="toc">
    IN_OL,       // Inside <ol>
    IN_LI,       // Inside <li>
    IN_ANCHOR,   // Inside <a>
  };

  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  enum XML_Error parseError = XML_ERROR_NONE;
  ParserState state = START;
  BookMetadataCache* cache;

  // Track nesting depth for <ol> elements to determine TOC depth
  uint8_t olDepth = 0;
  // Current entry data being collected
  std::string currentLabel;
  std::string currentHref;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

  size_t writeInternal(const uint8_t* buffer, size_t size, bool trackRemainingSize);

 public:
  explicit TocNavParser(const std::string& baseContentPath, const size_t xmlSize, BookMetadataCache* cache)
      : baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~TocNavParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Used only by the bounded malformed-ampersand retry path. It keeps the
  // parser open until finish() receives the explicit end-of-document marker.
  size_t writeStreaming(const uint8_t* buffer, size_t size);
  bool finish();
  bool shouldRetryWithAmpersandRecovery() const { return parseError == XML_ERROR_INVALID_TOKEN; }
};
