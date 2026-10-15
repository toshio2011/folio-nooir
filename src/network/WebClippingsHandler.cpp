#include "WebClippingsHandler.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <WebServer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "BookMetadataOverridesStore.h"
#include "BookStateStore.h"
#include "RecentBooksStore.h"
#include "util/ClipFile.h"
#include "html/ClippingsPageHtml.generated.h"

namespace {

constexpr size_t kPageLimit = 12;
constexpr int kMaxOffset = 100000;
constexpr size_t kRequestBodyLimit = 4096;
constexpr size_t kMinimumFreeHeap = 96 * 1024;
constexpr size_t kMinimumLargestBlock = 48 * 1024;

bool memoryAvailable(WebServer* server) {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largestBlock = ESP.getMaxAllocHeap();
  if (freeHeap >= kMinimumFreeHeap && largestBlock >= kMinimumLargestBlock) return true;
  server->send(503, "application/json", "{\"error\":\"Device memory is busy. Try again shortly.\"}");
  return false;
}

void sendPage(WebServer* server) {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "no-cache");
  server->sendHeader("X-Folio-Build", CROSSPOINT_VERSION);
  server->send_P(200, "text/html", ClippingsPageHtml, sizeof(ClippingsPageHtml));
}

bool parsePaging(WebServer* server, int& offset, size_t& limit) {
  offset = server->hasArg("offset") ? server->arg("offset").toInt() : 0;
  const int requestedLimit = server->hasArg("limit") ? server->arg("limit").toInt()
                                                       : static_cast<int>(kPageLimit);
  if (offset < 0 || offset > kMaxOffset || requestedLimit < 1) {
    server->send(400, "application/json", "{\"error\":\"Invalid page.\"}");
    return false;
  }
  limit = std::min(static_cast<size_t>(requestedLimit), kPageLimit);
  return true;
}

std::string titleForPath(const std::string& path, const std::string& storedTitle) {
  const auto* overrideData = BOOK_METADATA_OVERRIDES.find(path);
  if (overrideData) return overrideData->title;
  if (!storedTitle.empty()) return storedTitle;
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool validBookPath(const std::string& path) {
  if (path.empty() || path.size() > 240 || path.front() != '/' || path.rfind("/.crosspoint/", 0) == 0 ||
      path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
      !FsHelpers::hasEpubExtension(path)) {
    return false;
  }
  return Storage.exists(path.c_str());
}

bool recentContains(const std::string& path) {
  const auto& recentBooks = RECENT_BOOKS.getBooks();
  return std::any_of(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) {
    return book.path == path;
  });
}

// Visit existing EPUBs in the two stores the reader already maintains. No
// second library index is built, and each visitor loads at most one book's
// bounded clipping vector at a time.
template <typename Visitor>
void visitKnownBooks(Visitor&& visitor) {
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (!validBookPath(book.path)) continue;
    if (!visitor(book.path, titleForPath(book.path, book.title))) return;
  }
  for (const auto& state : BOOK_STATES.getBooks()) {
    if (recentContains(state.path) || !validBookPath(state.path)) continue;
    if (!visitor(state.path, titleForPath(state.path, {}))) return;
  }
}

void startJson(WebServer* server) {
  server->sendHeader("Cache-Control", "no-store");
  server->sendHeader("X-Folio-Build", CROSSPOINT_VERSION);
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
}

void appendRow(WebServer* server, JsonDocument& row, bool& first) {
  String json;
  serializeJson(row, json);
  if (!first) server->sendContent(",");
  server->sendContent(json);
  first = false;
}

void sendBooksPage(WebServer* server, const int offset, const size_t limit) {
  if (!memoryAvailable(server)) return;
  startJson(server);
  server->sendContent("{\"books\":[");
  bool first = true;
  bool hasMore = false;
  size_t visible = 0;
  size_t emitted = 0;
  JsonDocument row;
  visitKnownBooks([&](const std::string& path, const std::string& title) {
    size_t count = 0;
    if (!ClipFile::count(path, count) || count == 0) return true;
    if (visible++ < static_cast<size_t>(offset)) return true;
    if (emitted >= limit) {
      hasMore = true;
      return false;
    }
    row.clear();
    row["path"] = path;
    row["title"] = title;
    row["count"] = count;
    appendRow(server, row, first);
    ++emitted;
    return true;
  });
  char footer[112];
  std::snprintf(footer, sizeof(footer), "],\"offset\":%d,\"limit\":%u,\"hasMore\":%s}", offset,
           static_cast<unsigned>(limit), hasMore ? "true" : "false");
  server->sendContent(footer);
  server->sendContent("");
}

void appendClippingRow(WebServer* server, JsonDocument& row, bool& first, const std::string& path,
                       const std::string& title, const size_t index, const ClippingEntry& clip,
                       const std::string& note) {
  row.clear();
  row["path"] = path;
  row["title"] = title;
  row["index"] = index;
  row["text"] = clip.text;
  row["note"] = note;
  row["page"] = static_cast<unsigned>(clip.page) + 1;
  row["spine"] = clip.spineIndex;
  row["date"] = clip.dateKey;
  row["percentage"] = static_cast<unsigned>(clip.percentage * 100.0f);
  row["styleMask"] = clip.styleMask;
  row["bold"] = clip.bold;
  appendRow(server, row, first);
}

void sendClippingsPage(WebServer* server, const int offset, const size_t limit) {
  if (!server->hasArg("book")) {
    if (server->hasArg("books")) {
      sendBooksPage(server, offset, limit);
      return;
    }
    if (!memoryAvailable(server)) return;
    startJson(server);
    server->sendContent("{\"items\":[");
    bool first = true;
    bool hasMore = false;
    size_t emitted = 0;
    size_t remaining = static_cast<size_t>(offset);
    JsonDocument row;
    visitKnownBooks([&](const std::string& path, const std::string& title) {
      size_t bookCount = 0;
      if (!ClipFile::count(path, bookCount) || bookCount == 0) return true;
      if (remaining >= bookCount) {
        remaining -= bookCount;
        return true;
      }

      if (emitted >= limit) {
        hasMore = true;
        return false;
      }

      const size_t start = remaining;
      remaining = 0;
      const size_t requested = std::min(limit - emitted, bookCount - start);
      std::vector<ClippingEntry> clips;
      std::vector<std::string> notes;
      size_t loadedCount = 0;
      if (!ClipFile::loadPage(path, start, requested, clips, notes, loadedCount)) return true;
      for (size_t i = 0; i < clips.size(); ++i) {
        const std::string& note = i < notes.size() ? notes[i] : std::string{};
        appendClippingRow(server, row, first, path, title, start + i, clips[i], note);
        ++emitted;
      }
      if (start + clips.size() < loadedCount) {
        hasMore = true;
        return false;
      }
      return true;
    });
    char footer[112];
    std::snprintf(footer, sizeof(footer), "],\"offset\":%d,\"limit\":%u,\"hasMore\":%s}", offset,
             static_cast<unsigned>(limit), hasMore ? "true" : "false");
    server->sendContent(footer);
    server->sendContent("");
    return;
  }

  const std::string path = server->arg("book").c_str();
  if (!validBookPath(path)) {
    server->send(404, "application/json", "{\"error\":\"Book not found.\"}");
    return;
  }
  if (!memoryAvailable(server)) return;
  std::vector<ClippingEntry> clips;
  std::vector<std::string> notes;
  size_t totalCount = 0;
  if (!ClipFile::loadPage(path, static_cast<size_t>(offset), limit, clips, notes, totalCount)) {
    server->send(404, "application/json", "{\"error\":\"No saved clippings for this book.\"}");
    return;
  }

  const size_t start = static_cast<size_t>(offset);
  const std::string title = titleForPath(path, {});
  startJson(server);
  server->sendContent("{\"items\":[");
  bool first = true;
  JsonDocument row;
  for (size_t i = 0; i < clips.size(); ++i) {
    const std::string& note = i < notes.size() ? notes[i] : std::string{};
    appendClippingRow(server, row, first, path, title, start + i, clips[i], note);
  }
  char footer[112];
  std::snprintf(footer, sizeof(footer), "],\"offset\":%d,\"limit\":%u,\"hasMore\":%s}", offset,
           static_cast<unsigned>(limit), start + clips.size() < totalCount ? "true" : "false");
  server->sendContent(footer);
  server->sendContent("");
}

bool readRequestDocument(WebServer* server, JsonDocument& doc) {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"Missing JSON body.\"}");
    return false;
  }
  const String body = server->arg("plain");
  if (body.length() == 0 || body.length() > kRequestBodyLimit) {
    server->send(413, "application/json", "{\"error\":\"Request is too large.\"}");
    return false;
  }
  if (deserializeJson(doc, body)) {
    server->send(400, "application/json", "{\"error\":\"Invalid JSON body.\"}");
    return false;
  }
  return true;
}

void handlePost(WebServer* server) {
  if (!memoryAvailable(server)) return;
  JsonDocument doc;
  if (!readRequestDocument(server, doc)) return;
  const std::string action = doc["action"] | "";
  const std::string path = doc["path"] | "";
  const int index = doc["index"] | -1;
  if ((action != "edit" && action != "delete") || !validBookPath(path) || index < 0 ||
      static_cast<size_t>(index) >= ClipFile::MAX_CLIPPINGS_PER_BOOK) {
    server->send(400, "application/json", "{\"error\":\"Invalid clipping request.\"}");
    return;
  }

  const std::string expectedText = doc["expectedText"] | "";
  const std::string expectedNote = doc["expectedNote"] | "";
  const int expectedPage = doc["expectedPage"] | -1;
  const int expectedSpine = doc["expectedSpine"] | -1;
  std::vector<ClippingEntry> clips;
  std::vector<std::string> notes;
  if (!ClipFile::load(path, clips, notes) || static_cast<size_t>(index) >= clips.size()) {
    server->send(404, "application/json", "{\"error\":\"Clipping not found. Reload the page.\"}");
    return;
  }
  const std::string savedNote = static_cast<size_t>(index) < notes.size() ? notes[index] : std::string{};
  const bool expectedDateMissing = doc["expectedDate"].isNull();
  const uint32_t expectedDate = doc["expectedDate"] | static_cast<uint32_t>(0);
  if (clips[index].text != expectedText || savedNote != expectedNote || expectedPage < 1 ||
      static_cast<unsigned>(expectedPage) != static_cast<unsigned>(clips[index].page) + 1 ||
      expectedSpine < 0 || static_cast<unsigned>(expectedSpine) != clips[index].spineIndex || expectedDateMissing ||
      expectedDate != clips[index].dateKey) {
    server->send(409, "application/json", "{\"error\":\"This clipping changed. Reload the page before saving.\"}");
    return;
  }

  if (action == "edit") {
    const std::string text = doc["text"] | "";
    const std::string note = doc["note"] | "";
    if (text.empty() || text.size() > ClipFile::MAX_CLIPPING_TEXT_BYTES ||
        note.size() > ClipFile::MAX_NOTE_BYTES) {
      server->send(400, "application/json", "{\"error\":\"Quote or note is empty or too long.\"}");
      return;
    }
    clips[index].text = text;
    if (notes.size() < clips.size()) notes.resize(clips.size());
    notes[index] = note;
  } else {
    clips.erase(clips.begin() + index);
    if (static_cast<size_t>(index) < notes.size()) notes.erase(notes.begin() + index);
  }

  // ClipFile::replace updates only the existing per-book JSON and preserves
  // the append-only /My Clippings.txt export by design.
  if (!ClipFile::replace(path, clips, notes)) {
    server->send(500, "application/json", "{\"error\":\"Could not save clipping changes.\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

void WebClippingsHandler::registerRoutes(WebServer* server) {
  if (!server) return;
  server->on("/clippings", HTTP_GET, [server] { sendPage(server); });
  server->on("/api/clippings", HTTP_GET, [server] {
    int offset = 0;
    size_t limit = kPageLimit;
    if (!parsePaging(server, offset, limit)) return;
    sendClippingsPage(server, offset, limit);
  });
  server->on("/api/clippings", HTTP_POST, [server] { handlePost(server); });
}
