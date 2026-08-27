#pragma once

#include <cstring>

#include <expat.h>

// Expat is intentionally used without namespace processing in the EPUB
// parsers, so element names arrive as either "tag" or "prefix:tag". EPUB
// producers are allowed to choose the prefix; matching the local name keeps
// the parser compatible without changing the attribute representation used by
// the existing HTML parser.
inline const XML_Char* xmlLocalName(const XML_Char* name) {
  if (!name) return "";
  const XML_Char* separator = strrchr(name, ':');
  return separator ? separator + 1 : name;
}

inline bool xmlNameIs(const XML_Char* name, const char* wantedLocalName) {
  return name && wantedLocalName && strcmp(xmlLocalName(name), wantedLocalName) == 0;
}

// Safely tear down an expat parser: stop processing, clear callbacks, free, and null the pointer.
inline void destroyXmlParser(XML_Parser& parser) {
  if (!parser) return;
  XML_StopParser(parser, XML_FALSE);
  XML_SetElementHandler(parser, nullptr, nullptr);
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  parser = nullptr;
}
