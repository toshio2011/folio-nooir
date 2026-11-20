#include <BoundedWebJsonWriter.h>
#include <BoundedWebChunkWriter.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {
struct FixedSink {
  std::string output;
  std::vector<size_t> chunks;
  size_t maxAcceptedChunk = 256;
  size_t remainingByteBudget = std::numeric_limits<size_t>::max();
  size_t acceptedChunksBeforeFailure = std::numeric_limits<size_t>::max();
  bool fail = false;

  static bool write(void* context, const char* bytes, const size_t length) {
    auto& self = *static_cast<FixedSink*>(context);
    self.chunks.push_back(length);
    if (self.fail || self.chunks.size() > self.acceptedChunksBeforeFailure ||
        length > self.maxAcceptedChunk || length > self.remainingByteBudget) return false;
    self.output.append(bytes, length);
    self.remainingByteBudget -= length;
    return true;
  }
};

struct FixedNetworkClient {
  std::string output;
  size_t shortWriteAt = std::numeric_limits<size_t>::max();
  size_t writeCalls = 0;
  bool isConnected = true;

  bool connected() const { return isConnected; }

  size_t write(const uint8_t* bytes, const size_t length) {
    const size_t accepted = writeCalls++ == shortWriteAt && length > 0 ? length - 1 : length;
    output.append(reinterpret_cast<const char*>(bytes), accepted);
    return accepted;
  }

  size_t write(const char* bytes, const size_t length) {
    return write(reinterpret_cast<const uint8_t*>(bytes), length);
  }
};

TEST(BoundedWebChunkWriter, EmitsValidChunkFramingWithoutHeapBuffers) {
  FixedNetworkClient client;

  ASSERT_TRUE(writeBoundedWebChunk(client, "hello", 5));
  EXPECT_EQ(client.output, "5\r\nhello\r\n");
  EXPECT_EQ(client.writeCalls, 3U);
}

TEST(BoundedWebChunkWriter, StopsOnShortHeaderPayloadOrTerminatorWrite) {
  for (size_t shortWriteAt = 0; shortWriteAt < 3; ++shortWriteAt) {
    FixedNetworkClient client;
    client.shortWriteAt = shortWriteAt;

    EXPECT_FALSE(writeBoundedWebChunk(client, "hello", 5));
    EXPECT_EQ(client.writeCalls, shortWriteAt + 1);
  }
}

TEST(BoundedWebChunkWriter, RejectsDisconnectedClientBeforeWriting) {
  FixedNetworkClient client;
  client.isConnected = false;

  EXPECT_FALSE(writeBoundedWebChunk(client, "hello", 5));
  EXPECT_TRUE(client.output.empty());
  EXPECT_EQ(client.writeCalls, 0U);
}

TEST(BoundedWebJsonWriter, EscapesControlsAndPreservesUtf8) {
  FixedSink sink;
  BoundedWebJsonWriter<8> writer(&FixedSink::write, &sink);
  const std::string input = std::string("a\"\\\n\t\x01", 6) + " 日本語";

  ASSERT_TRUE(writer.string(input));
  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(sink.output, "\"a\\\"\\\\\\n\\t\\u0001 日本語\"");
  ASSERT_FALSE(sink.chunks.empty());
  EXPECT_LE(*std::max_element(sink.chunks.begin(), sink.chunks.end()), 8U);
}

TEST(BoundedWebJsonWriter, StreamsLargeStringsInFixedChunks) {
  FixedSink sink;
  BoundedWebJsonWriter<32> writer(&FixedSink::write, &sink);
  std::string value(2048, 'x');

  ASSERT_TRUE(writer.raw("{\"value\":"));
  ASSERT_TRUE(writer.string(value));
  ASSERT_TRUE(writer.raw("}"));
  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(sink.output, "{\"value\":\"" + value + "\"}");
  ASSERT_FALSE(sink.chunks.empty());
  EXPECT_LE(*std::max_element(sink.chunks.begin(), sink.chunks.end()), 32U);
}

TEST(BoundedWebJsonWriter, WritesIntegerBoundariesAndJsonBoolean) {
  FixedSink sink;
  BoundedWebJsonWriter<16> writer(&FixedSink::write, &sink);

  ASSERT_TRUE(writer.unsignedNumber(std::numeric_limits<uint64_t>::max()));
  ASSERT_TRUE(writer.raw(","));
  ASSERT_TRUE(writer.signedNumber(std::numeric_limits<int64_t>::min()));
  ASSERT_TRUE(writer.raw(","));
  ASSERT_TRUE(writer.boolean(true));
  ASSERT_TRUE(writer.raw(","));
  ASSERT_TRUE(writer.boolean(false));
  ASSERT_TRUE(writer.finish());
  EXPECT_EQ(sink.output, "18446744073709551615,-9223372036854775808,true,false");
}

TEST(BoundedWebJsonWriter, StopsWritingAfterSinkFailure) {
  FixedSink sink;
  sink.fail = true;
  BoundedWebJsonWriter<4> writer(&FixedSink::write, &sink);

  EXPECT_FALSE(writer.raw("abcd"));
  EXPECT_FALSE(writer.raw("more"));
  EXPECT_FALSE(writer.finish());
  EXPECT_FALSE(writer.good());
  EXPECT_TRUE(sink.output.empty());
  EXPECT_EQ(sink.chunks.size(), 1U);
}

TEST(BoundedWebJsonWriter, StopsAfterSimulatedLowMemoryRejectsAChunk) {
  FixedSink sink;
  sink.remainingByteBudget = 8;
  BoundedWebJsonWriter<4> writer(&FixedSink::write, &sink);

  EXPECT_FALSE(writer.raw("abcdefghijkl"));
  EXPECT_FALSE(writer.finish());
  EXPECT_FALSE(writer.good());
  EXPECT_EQ(sink.output, "abcdefgh");
  EXPECT_EQ(sink.chunks.size(), 3U);
}

TEST(BoundedWebJsonWriter, StopsAfterClientDisconnectAndAllowsNextRequest) {
  FixedSink disconnected;
  disconnected.acceptedChunksBeforeFailure = 1;
  BoundedWebJsonWriter<4> failedWriter(&FixedSink::write, &disconnected);

  EXPECT_FALSE(failedWriter.raw("abcdefghijkl"));
  EXPECT_FALSE(failedWriter.finish());
  EXPECT_EQ(disconnected.output, "abcd");
  EXPECT_EQ(disconnected.chunks.size(), 2U);

  FixedSink nextRequest;
  BoundedWebJsonWriter<4> nextWriter(&FixedSink::write, &nextRequest);
  ASSERT_TRUE(nextWriter.raw("{\"ok\":true}"));
  ASSERT_TRUE(nextWriter.finish());
  EXPECT_EQ(nextRequest.output, "{\"ok\":true}");
}

TEST(BoundedWebJsonWriter, RepeatedRequestsStartWithEmptyWriterState) {
  for (size_t request = 0; request < 32; ++request) {
    FixedSink sink;
    BoundedWebJsonWriter<8> writer(&FixedSink::write, &sink);
    ASSERT_TRUE(writer.raw("{\"books\":[]}"));
    ASSERT_TRUE(writer.finish());
    EXPECT_EQ(sink.output, "{\"books\":[]}");
  }
}
}  // namespace
