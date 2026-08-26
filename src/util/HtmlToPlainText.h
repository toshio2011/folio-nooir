#pragma once

#include <string>

// Convert an HTML fragment to readable plain text. CSS styling is flattened
// for e-ink, but style/script/head blocks are removed, block/table/list
// elements become line breaks, and HTML entities are decoded.
std::string htmlToPlainText(const std::string& html);
