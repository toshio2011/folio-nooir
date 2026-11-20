import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = (ROOT / "src/network/CrossPointWebServer.cpp").read_text(encoding="utf-8")
STORE_H = (ROOT / "src/RecentBooksStore.h").read_text(encoding="utf-8")
STORE_CPP = (ROOT / "src/RecentBooksStore.cpp").read_text(encoding="utf-8")
UI_THEME = (ROOT / "src/components/UITheme.cpp").read_text(encoding="utf-8")


def route_body(name: str, next_name: str) -> str:
    start = SERVER.index(f"void CrossPointWebServer::{name}() const")
    end = SERVER.index(f"void CrossPointWebServer::{next_name}() const", start)
    return SERVER[start:end]


class WebStreamingAllocationContract(unittest.TestCase):
    def test_stream_routes_reuse_web_memory_floor_and_return_small_json_error(self):
        start = SERVER.index("bool webStreamMemoryAdmitted(")
        end = SERVER.index("\n}\n", start) + 2
        guard = SERVER[start:end]
        self.assertIn("ESP.getFreeHeap()", guard)
        self.assertIn("ESP.getMaxAllocHeap()", guard)
        self.assertIn("WebSettingsMemoryPolicy::admitted(freeHeap, largest, responseBytes)", guard)
        self.assertIn('server->send(503, "application/json", "{\\\"error\\\":\\\"temporarily_unavailable\\\"}")', guard)

        for route_name, next_name in (("handleLibraryData", "handleLibraryCover"),
                                      ("handleStatsData", "handleGetBookInfo")):
            body = route_body(route_name, next_name)
            self.assertLess(body.index("webStreamMemoryAdmitted"), body.index("beginBoundedJsonResponse"))

    def test_library_route_uses_narrow_projection_and_no_row_documents(self):
        body = route_body("handleLibraryData", "handleLibraryCover")
        self.assertIn("getLibraryApiEntryAt", body)
        self.assertIn("BoundedWebJsonWriter", body)
        self.assertNotIn("JsonDocument", body)
        self.assertNotIn("String row", body)
        self.assertNotIn("getBookAt", body)

    def test_repeated_library_rows_reuse_projection_and_thumbnail_string_storage(self):
        body = route_body("handleLibraryData", "handleLibraryCover")
        loop = body.index("for (size_t rawIndex = 0; rawIndex < rawBookCount")
        self.assertLess(body.index("RecentBookLibraryApiEntry book;"), loop)
        self.assertLess(body.index("std::string thumbnailPath;"), loop)
        self.assertIn("getCoverThumbPath(book.coverBmpPath, coverHeight, thumbnailPath)", body)
        self.assertIn("destination.assign(coverBmpPath)", UI_THEME)
        self.assertNotIn("std::to_string(coverHeight)", UI_THEME)

    def test_stats_route_uses_projection_and_nonallocating_path_lookup(self):
        body = route_body("handleStatsData", "handleGetBookInfo")
        self.assertIn("getStatsApiEntryAt", body)
        self.assertIn("containsPath", body)
        self.assertIn("BoundedWebJsonWriter", body)
        self.assertNotIn("JsonDocument", body)
        self.assertNotIn("String row", body)
        self.assertNotIn("copyRecentBookByPath", body)

    def test_bounded_json_responses_use_fixed_stack_chunk_framing(self):
        library = route_body("handleLibraryData", "handleLibraryCover")
        stats = route_body("handleStatsData", "handleGetBookInfo")
        response_helpers = SERVER[SERVER.index("bool finishBoundedJsonResponse"):
                                  SERVER.index("void closeFileIfOpen")]
        self.assertIn("beginBoundedJsonResponse(server.get())", library)
        self.assertIn("beginBoundedJsonResponse(server.get())", stats)
        self.assertIn('webServer->chunkResponseBegin("application/json")', response_helpers)
        self.assertIn("webServer->chunkResponseEnd()", response_helpers)
        self.assertNotIn("setContentLength(CONTENT_LENGTH_UNKNOWN)", response_helpers)
        self.assertIn("writeBoundedWebChunk(webServer->client(), bytes, length)", SERVER)
        self.assertIn('snprintf(chunkSize, sizeof(chunkSize), "%zx\\r\\n", length)',
                      (ROOT / "src/network/BoundedWebChunkWriter.h").read_text(encoding="utf-8"))
        self.assertNotIn("chunkWrite(bytes, length)", SERVER)
        self.assertNotIn("sendContent(bytes, length)", SERVER)

    def test_repeated_stats_rows_reuse_projection_storage(self):
        body = route_body("handleStatsData", "handleGetBookInfo")
        loop = body.index("for (int index = 0; index < recentCount")
        self.assertLess(body.index("RecentBookStatsApiEntry recent;"), loop)

    def test_recent_store_projection_methods_copy_only_route_fields(self):
        self.assertIn("RecentBookLibraryApiEntry", STORE_H)
        self.assertIn("RecentBookStatsApiEntry", STORE_H)
        self.assertIn("destination.synopsis = book.synopsis", STORE_CPP)
        self.assertIn("destination.coverBmpPath = book.coverBmpPath", STORE_CPP)
        self.assertIn("bool RecentBooksStore::containsPath", STORE_CPP)


if __name__ == "__main__":
    unittest.main()
