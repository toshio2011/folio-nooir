#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "HalStorage.h"
#include "HttpDownloader.h"
#include "HttpTransferPolicy.h"
#include "SecureHttpClient.h"

namespace {
using Client = freeink::SecureHttpClient;

Client::Response response(const int status, const std::string& body = {}, const std::string& location = {}) {
  Client::Response value;
  value.status = status;
  value.body = body;
  value.location = location;
  return value;
}

class OpdsHttpDownloaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Client::reset();
    Storage.clear();
  }
};

TEST_F(OpdsHttpDownloaderTest, StatusAndRedirectPolicyBoundariesAreExplicit) {
  for (const int status : {401, 403, 404, 500, 503}) {
    Client::setResponses({response(status)});
    EXPECT_FALSE(HttpDownloader::fetchUrl("http://catalog.example/feed", [](const uint8_t*, size_t) { return true; }))
        << "status " << status;
  }
  EXPECT_TRUE(HttpTransferPolicy::isSuccessfulStatus(200));
  EXPECT_FALSE(HttpTransferPolicy::isSuccessfulStatus(204));
  EXPECT_TRUE(HttpTransferPolicy::mayFollowRedirect(302, 0));
  EXPECT_TRUE(HttpTransferPolicy::mayFollowRedirect(308, HttpTransferPolicy::MAX_REDIRECTS - 1));
  EXPECT_FALSE(HttpTransferPolicy::mayFollowRedirect(302, HttpTransferPolicy::MAX_REDIRECTS));
  EXPECT_FALSE(HttpTransferPolicy::mayFollowRedirect(200, 0));
}

TEST_F(OpdsHttpDownloaderTest, SendsBasicAuthIncludingAnEmptyPassword) {
  Client::setResponses({response(200, "ok")});
  std::string body;
  ASSERT_TRUE(HttpDownloader::fetchUrl(
      "http://catalog.example/feed", [&body](const uint8_t* data, size_t size) {
        body.append(reinterpret_cast<const char*>(data), size);
        return true;
      },
      "reader", ""));
  ASSERT_EQ(Client::requests.size(), 1u);
  EXPECT_NE(std::find(Client::requests[0].headers.begin(), Client::requests[0].headers.end(),
                      "Authorization: Basic cmVhZGVyOg==\r\n"),
            Client::requests[0].headers.end());
  EXPECT_EQ(body, "ok");
}

TEST_F(OpdsHttpDownloaderTest, ResolvesNestedRedirectsAndStopsAtTheConfiguredLimit) {
  Client::setResponses({response(302, {}, "section/one.xml"), response(302, {}, "../two.xml"),
                        response(302, {}, "?page=3"), response(302, {}, "//catalog.example/final.xml"),
                        response(302, {}, "/last.xml"), response(200, "feed")});
  std::string body;
  ASSERT_TRUE(HttpDownloader::fetchUrl(
      "http://catalog.example/catalog/index.xml", [&body](const uint8_t* data, size_t size) {
        body.append(reinterpret_cast<const char*>(data), size);
        return true;
      }));
  ASSERT_EQ(Client::requests.size(), 6u);
  EXPECT_EQ(Client::requests[1].url, "http://catalog.example/catalog/section/one.xml");
  EXPECT_EQ(Client::requests[2].url, "http://catalog.example/catalog/two.xml");
  EXPECT_EQ(Client::requests[3].url, "http://catalog.example/catalog/two.xml?page=3");
  EXPECT_EQ(Client::requests[4].url, "http://catalog.example/final.xml");
  EXPECT_EQ(Client::requests[5].url, "http://catalog.example/last.xml");
  EXPECT_EQ(body, "feed");
}

TEST_F(OpdsHttpDownloaderTest, RedirectLoopStopsAfterFiveHops) {
  std::vector<Client::Response> responses;
  for (int i = 0; i < 6; ++i) responses.push_back(response(302, {}, "/loop"));
  Client::setResponses(std::move(responses));
  EXPECT_FALSE(HttpDownloader::fetchUrl("http://catalog.example/feed", [](const uint8_t*, size_t) { return true; }));
  EXPECT_EQ(Client::requests.size(), 6u);
}

TEST_F(OpdsHttpDownloaderTest, RefusesHttpsDowngradeForNormalCatalogFetches) {
  Client::setResponses({response(302, {}, "http://catalog.example/insecure")});
  EXPECT_FALSE(HttpDownloader::fetchUrl("https://catalog.example/feed", [](const uint8_t*, size_t) { return true; }));
  EXPECT_EQ(Client::requests.size(), 1u);
}

TEST_F(OpdsHttpDownloaderTest, DoesNotForwardBasicCredentialsToAnotherOrigin) {
  Client::setResponses({response(302, {}, "https://cdn.example/book.epub"), response(200, "book")});
  ASSERT_TRUE(HttpDownloader::fetchUrl("https://catalog.example/feed", [](const uint8_t*, size_t) { return true; },
                                       "reader", "secret"));
  ASSERT_EQ(Client::requests.size(), 2u);
  EXPECT_TRUE(std::any_of(Client::requests[0].headers.begin(), Client::requests[0].headers.end(),
                          [](const std::string& header) { return header.rfind("Authorization:", 0) == 0; }));
  EXPECT_FALSE(std::any_of(Client::requests[1].headers.begin(), Client::requests[1].headers.end(),
                           [](const std::string& header) { return header.rfind("Authorization:", 0) == 0; }));
}

TEST_F(OpdsHttpDownloaderTest, StreamsChunkedBodiesWithoutAContentLength) {
  auto chunked = response(200, "abcdefgh");
  chunked.hasLength = false;
  chunked.chunkSize = 2;
  Client::setResponses({chunked});
  std::string received;
  ASSERT_TRUE(HttpDownloader::fetchUrl(
      "http://catalog.example/feed", [&received](const uint8_t* data, size_t size) {
        received.append(reinterpret_cast<const char*>(data), size);
        return true;
      }));
  EXPECT_EQ(received, "abcdefgh");
}

TEST_F(OpdsHttpDownloaderTest, RejectsIncompleteResponseEvenAfterReceivingSomeBody) {
  auto truncated = response(200, "partial");
  truncated.complete = false;
  Client::setResponses({truncated});
  std::string received;
  EXPECT_FALSE(HttpDownloader::fetchUrl(
      "http://catalog.example/feed", [&received](const uint8_t* data, size_t size) {
        received.append(reinterpret_cast<const char*>(data), size);
        return true;
      }));
  EXPECT_EQ(received, "partial");
  EXPECT_FALSE(HttpTransferPolicy::isResponseComplete(false, 7, 7));
  EXPECT_FALSE(HttpTransferPolicy::isResponseComplete(true, 8, 7));
  EXPECT_TRUE(HttpTransferPolicy::isResponseComplete(true, 0, 7));
}

TEST_F(OpdsHttpDownloaderTest, CancellationDuringFeedStopsBeforeTheNextChunk) {
  auto slowBody = response(200, "abcdefgh");
  slowBody.chunkSize = 2;
  Client::setResponses({slowBody});
  bool cancelled = false;
  std::string received;
  EXPECT_FALSE(HttpDownloader::fetchUrl(
      "http://catalog.example/feed",
      [&cancelled, &received](const uint8_t* data, size_t size) {
        received.append(reinterpret_cast<const char*>(data), size);
        cancelled = true;
        return true;
      },
      "", "", &cancelled));
  EXPECT_EQ(received, "ab");
}

TEST_F(OpdsHttpDownloaderTest, SlowTransferTimeoutRemainsSixtySeconds) {
  Client::setResponses({response(200, "slow body")});
  ASSERT_TRUE(HttpDownloader::fetchUrl("http://catalog.example/feed", [](const uint8_t*, size_t) { return true; }));
  ASSERT_EQ(Client::requests.size(), 1u);
  EXPECT_EQ(Client::requests[0].timeoutMs, 60000u);
}

TEST_F(OpdsHttpDownloaderTest, ZeroByteAndIncompleteDownloadsRemoveTemporaryFiles) {
  Client::setResponses({response(200)});
  EXPECT_EQ(HttpDownloader::downloadToFile("http://catalog.example/empty.epub", "/books/empty.part"),
            HttpDownloader::HTTP_ERROR);
  EXPECT_FALSE(Storage.exists("/books/empty.part"));

  auto incomplete = response(200, "part");
  incomplete.complete = false;
  Client::setResponses({incomplete});
  EXPECT_EQ(HttpDownloader::downloadToFile("http://catalog.example/book.epub", "/books/book.part"),
            HttpDownloader::HTTP_ERROR);
  EXPECT_FALSE(Storage.exists("/books/book.part"));
}

TEST_F(OpdsHttpDownloaderTest, CancellationDuringDownloadRemovesPartialFile) {
  auto body = response(200, "abcdefgh");
  body.chunkSize = 2;
  Client::setResponses({body});
  bool cancelled = false;
  const auto result = HttpDownloader::downloadToFile(
      "http://catalog.example/book.epub", "/books/book.part",
      [&cancelled](size_t, size_t) { cancelled = true; }, &cancelled);
  EXPECT_EQ(result, HttpDownloader::ABORTED);
  EXPECT_FALSE(Storage.exists("/books/book.part"));
}

TEST_F(OpdsHttpDownloaderTest, ChunkedDownloadReportsIndeterminateProgressPerChunk) {
  auto body = response(200, "abcdefgh");
  body.hasLength = false;
  body.chunkSize = 2;
  Client::setResponses({body});
  std::vector<std::pair<size_t, size_t>> progress;
  ASSERT_EQ(HttpDownloader::downloadToFile(
                "http://catalog.example/book.epub", "/books/book.epub",
                [&progress](const size_t downloaded, const size_t total) { progress.emplace_back(downloaded, total); }),
            HttpDownloader::OK);
  EXPECT_EQ(progress, (std::vector<std::pair<size_t, size_t>>{{2, 0}, {4, 0}, {6, 0}, {8, 0}}));
}
}  // namespace
