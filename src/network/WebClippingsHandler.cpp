#include "WebClippingsHandler.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebServer.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "BookMetadataOverridesStore.h"
#include "BookStateStore.h"
#include "ClippingsApiPaging.h"
#include "RecentBooksStore.h"
#include "html/ClippingsPageHtml.generated.h"
#include "util/ClipFile.h"

namespace {

constexpr size_t kRequestBodyLimit = 4096;
constexpr size_t kMinimumFreeHeap = 96 * 1024;
constexpr size_t kMinimumLargestBlock = 48 * 1024;

bool memoryAvailable(WebServer* server, const char* route, const char* book, const int offset) {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largestBlock = ESP.getMaxAllocHeap();
  if (freeHeap >= kMinimumFreeHeap && largestBlock >= kMinimumLargestBlock) return true;
  LOG_ERR("CLPWEB",
          "route=%s book=%s offset=%d records_found=0 response_count=0 response_status=503 free=%lu largest=%lu",
          route, book ? book : "", offset, static_cast<unsigned long>(freeHeap),
          static_cast<unsigned long>(largestBlock));
  server->send(503, "application/json", "{\"error\":\"Device memory is busy. Try again shortly.\"}");
  return false;
}

void sendPage(WebServer* server) {
  server->sendHeader("Content-Encoding", "gzip");
  server->sendHeader("Cache-Control", "no-cache");
  server->sendHeader("X-Folio-Build", CROSSPOINT_VERSION);
  server->send_P(200, "text/html", ClippingsPageHtml, sizeof(ClippingsPageHtml));
  LOG_INF("CLPWEB",
          "route=/clippings method=GET book= page=html offset=0 records_found=0 response_count=1 response_status=200");
}

bool parsePaging(WebServer* server, WebClippingsApi::PageWindow& page) {
  const int offset = server->hasArg("offset") ? server->arg("offset").toInt() : 0;
  const int requestedLimit = server->hasArg("limit") ? server->arg("limit").toInt()
                                                       : static_cast<int>(WebClippingsApi::kPageLimit);
  if (!WebClippingsApi::makePageWindow(offset, requestedLimit, page)) {
    const std::string book = server->hasArg("book") ? std::string(server->arg("book").c_str()) : std::string{};
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=GET book=%s page=invalid offset=%d records_found=0 response_count=0 response_status=400",
            book.c_str(), offset);
    server->send(400, "application/json", "{\"error\":\"Invalid page.\"}");
    return false;
  }
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

void sendBooksPage(WebServer* server, const size_t offset, const size_t limit) {
  if (!memoryAvailable(server, "/api/clippings?books", "*", static_cast<int>(offset))) return;
  startJson(server);
  server->sendContent("{\"books\":[");
  bool first = true;
  bool hasMore = false;
  size_t visible = 0;
  size_t emitted = 0;
  size_t recordsFound = 0;
  size_t storageErrors = 0;
  JsonDocument row;
  visitKnownBooks([&](const std::string& path, const std::string& title) {
    size_t count = 0;
    if (!ClipFile::count(path, count)) {
      ++storageErrors;
      return true;
    }
    recordsFound += count;
    if (count == 0) return true;
    if (visible++ < offset) return true;
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
  char footer[160];
  std::snprintf(footer, sizeof(footer), "],\"offset\":%u,\"limit\":%u,\"hasMore\":%s,\"storageErrors\":%u,\"recordsFound\":%u}",
                static_cast<unsigned>(offset), static_cast<unsigned>(limit), hasMore ? "true" : "false",
                static_cast<unsigned>(storageErrors), static_cast<unsigned>(recordsFound));
  server->sendContent(footer);
  server->sendContent("");
  LOG_INF("CLPWEB",
          "route=/api/clippings?books method=GET book=* page=%u offset=%u limit=%u records_found=%u response_count=%u storage_errors=%u response_status=200",
          static_cast<unsigned>(offset / WebClippingsApi::kPageLimit + 1), static_cast<unsigned>(offset),
          static_cast<unsigned>(limit), static_cast<unsigned>(recordsFound), static_cast<unsigned>(emitted),
          static_cast<unsigned>(storageErrors));
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

void sendClippingsPage(WebServer* server, const WebClippingsApi::PageWindow& page) {
  const size_t offset = page.offset;
  const size_t limit = page.limit;
  if (!server->hasArg("book")) {
    if (server->hasArg("books")) {
      sendBooksPage(server, offset, limit);
      return;
    }
    if (!memoryAvailable(server, "/api/clippings", "*", static_cast<int>(offset))) return;
    startJson(server);
    server->sendContent("{\"items\":[");
    bool first = true;
    JsonDocument row;
    const WebClippingsApi::PageSummary summary = WebClippingsApi::streamAllBooksPage(
        offset, limit,
        [](auto&& visitor) { visitKnownBooks(std::forward<decltype(visitor)>(visitor)); },
        [](const std::string& path, size_t& count) { return ClipFile::count(path, count); },
        [](const std::string& path, const size_t start, const size_t requested,
           std::vector<ClippingEntry>& clips, std::vector<std::string>& notes, size_t& totalCount) {
          return ClipFile::loadPage(path, start, requested, clips, notes, totalCount);
        },
        [&](const std::string& path, const std::string& title, const size_t index, const ClippingEntry& clip,
            const std::string& note) { appendClippingRow(server, row, first, path, title, index, clip, note); });
    char footer[160];
    std::snprintf(footer, sizeof(footer), "],\"offset\":%u,\"limit\":%u,\"hasMore\":%s,\"storageErrors\":%u,\"recordsFound\":%u}",
                  static_cast<unsigned>(offset), static_cast<unsigned>(limit), summary.hasMore ? "true" : "false",
                  static_cast<unsigned>(summary.storageErrors), static_cast<unsigned>(summary.recordsFound));
    server->sendContent(footer);
    server->sendContent("");
    LOG_INF("CLPWEB",
            "route=/api/clippings method=GET book=* page=%u offset=%u limit=%u records_found=%u response_count=%u storage_errors=%u response_status=200",
            static_cast<unsigned>(offset / WebClippingsApi::kPageLimit + 1), static_cast<unsigned>(offset),
            static_cast<unsigned>(limit), static_cast<unsigned>(summary.recordsFound),
            static_cast<unsigned>(summary.responseCount), static_cast<unsigned>(summary.storageErrors));
    return;
  }

  const std::string path = server->arg("book").c_str();
  if (!validBookPath(path)) {
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=GET book=%s page=%u offset=%u records_found=0 response_count=0 response_status=404 reason=invalid_or_missing_book",
            path.c_str(), static_cast<unsigned>(offset / WebClippingsApi::kPageLimit + 1),
            static_cast<unsigned>(offset));
    server->send(404, "application/json", "{\"error\":\"Book not found.\"}");
    return;
  }
  if (!memoryAvailable(server, "/api/clippings", path.c_str(), static_cast<int>(offset))) return;

  std::vector<ClippingEntry> clips;
  std::vector<std::string> notes;
  const WebClippingsApi::PageSummary summary = WebClippingsApi::loadBookPage(
      path, offset, limit, clips, notes,
      [](const std::string& bookPath, const size_t start, const size_t requested,
         std::vector<ClippingEntry>& pageClippings, std::vector<std::string>& pageNotes, size_t& totalCount) {
        return ClipFile::loadPage(bookPath, start, requested, pageClippings, pageNotes, totalCount);
      });
  if (summary.storageErrors != 0) {
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=GET book=%s page=%u offset=%u records_found=0 response_count=0 response_status=404 reason=clip_file_missing_or_unreadable",
            path.c_str(), static_cast<unsigned>(offset / WebClippingsApi::kPageLimit + 1),
            static_cast<unsigned>(offset));
    server->send(404, "application/json", "{\"error\":\"No saved clippings for this book.\"}");
    return;
  }

  const std::string title = titleForPath(path, {});
  startJson(server);
  server->sendContent("{\"items\":[");
  bool first = true;
  JsonDocument row;
  for (size_t index = 0; index < clips.size(); ++index) {
    static const std::string emptyNote;
    const std::string& note = index < notes.size() ? notes[index] : emptyNote;
    appendClippingRow(server, row, first, path, title, offset + index, clips[index], note);
  }
  char footer[112];
  std::snprintf(footer, sizeof(footer), "],\"offset\":%u,\"limit\":%u,\"hasMore\":%s,\"recordsFound\":%u}",
                static_cast<unsigned>(offset), static_cast<unsigned>(limit), summary.hasMore ? "true" : "false",
                static_cast<unsigned>(summary.recordsFound));
  server->sendContent(footer);
  server->sendContent("");
  LOG_INF("CLPWEB",
          "route=/api/clippings method=GET book=%s page=%u offset=%u limit=%u records_found=%u response_count=%u response_status=200",
          path.c_str(), static_cast<unsigned>(offset / WebClippingsApi::kPageLimit + 1),
          static_cast<unsigned>(offset), static_cast<unsigned>(limit), static_cast<unsigned>(summary.recordsFound),
          static_cast<unsigned>(summary.responseCount));
}

bool readRequestDocument(WebServer* server, JsonDocument& doc) {
  if (!server->hasArg("plain")) {
    LOG_ERR("CLPWEB", "route=/api/clippings method=POST book= page=request records_found=0 response_count=0 response_status=400 reason=missing_json_body");
    server->send(400, "application/json", "{\"error\":\"Missing JSON body.\"}");
    return false;
  }
  const String body = server->arg("plain");
  if (body.length() == 0 || body.length() > kRequestBodyLimit) {
    LOG_ERR("CLPWEB", "route=/api/clippings method=POST book= page=request records_found=0 response_count=0 response_status=413 reason=body_limit");
    server->send(413, "application/json", "{\"error\":\"Request is too large.\"}");
    return false;
  }
  if (deserializeJson(doc, body)) {
    LOG_ERR("CLPWEB", "route=/api/clippings method=POST book= page=request records_found=0 response_count=0 response_status=400 reason=invalid_json");
    server->send(400, "application/json", "{\"error\":\"Invalid JSON body.\"}");
    return false;
  }
  return true;
}

void handlePost(WebServer* server) {
  if (!memoryAvailable(server, "/api/clippings", "", -1)) return;
  JsonDocument doc;
  if (!readRequestDocument(server, doc)) return;
  const std::string action = doc["action"] | "";
  const std::string path = doc["path"] | "";
  const int index = doc["index"] | -1;
  if ((action != "edit" && action != "delete") || !validBookPath(path) || index < 0 ||
      static_cast<size_t>(index) >= ClipFile::MAX_CLIPPINGS_PER_BOOK) {
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=POST action=%s book=%s page=index:%d records_found=0 response_count=0 response_status=400",
            action.c_str(), path.c_str(), index);
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
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=POST action=%s book=%s page=index:%d records_found=%u response_count=0 response_status=404",
            action.c_str(), path.c_str(), index, static_cast<unsigned>(clips.size()));
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
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=POST action=%s book=%s page=index:%d records_found=%u response_count=0 response_status=409 reason=stale_record",
            action.c_str(), path.c_str(), index, static_cast<unsigned>(clips.size()));
    server->send(409, "application/json", "{\"error\":\"This clipping changed. Reload the page before saving.\"}");
    return;
  }

  if (action == "edit") {
    const std::string text = doc["text"] | "";
    const std::string note = doc["note"] | "";
    if (text.empty() || text.size() > ClipFile::MAX_CLIPPING_TEXT_BYTES ||
        note.size() > ClipFile::MAX_NOTE_BYTES) {
      LOG_ERR("CLPWEB",
              "route=/api/clippings method=POST action=edit book=%s page=index:%d records_found=%u response_count=0 response_status=400 reason=field_bounds",
              path.c_str(), index, static_cast<unsigned>(clips.size()));
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
    LOG_ERR("CLPWEB",
            "route=/api/clippings method=POST action=%s book=%s page=index:%d records_found=%u response_count=0 response_status=500 reason=save_failed",
            action.c_str(), path.c_str(), index, static_cast<unsigned>(clips.size()));
    server->send(500, "application/json", "{\"error\":\"Could not save clipping changes.\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
  LOG_INF("CLPWEB",
          "route=/api/clippings method=POST action=%s book=%s page=index:%d records_found=%u response_count=1 response_status=200",
          action.c_str(), path.c_str(), index, static_cast<unsigned>(clips.size()));
}

}  // namespace

void WebClippingsHandler::registerRoutes(WebServer* server) {
  if (!server) return;
  server->on("/clippings", HTTP_GET, [server] { sendPage(server); });
  server->on("/api/clippings", HTTP_GET, [server] {
    WebClippingsApi::PageWindow page;
    if (!parsePaging(server, page)) return;
    const std::string book = server->hasArg("book") ? std::string(server->arg("book").c_str()) : "*";
    LOG_INF("CLPWEB", "route=/api/clippings method=GET book=%s page=%u offset=%u limit=%u phase=request",
            book.c_str(), static_cast<unsigned>(page.offset / WebClippingsApi::kPageLimit + 1),
            static_cast<unsigned>(page.offset), static_cast<unsigned>(page.limit));
    sendClippingsPage(server, page);
  });
  server->on("/api/clippings", HTTP_POST, [server] { handlePost(server); });
}
