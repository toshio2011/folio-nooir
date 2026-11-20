#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Emit one HTTP/1.1 transfer-encoded chunk without allocating a String or
// heap-based chunk header. Client is expected to provide connected() and
// Print-compatible write() overloads.
template <typename Client>
bool writeBoundedWebChunk(Client& client, const char* bytes, const size_t length) {
  if (!client.connected()) return false;

  char chunkSize[11];
  const int chunkSizeLength = std::snprintf(chunkSize, sizeof(chunkSize), "%zx\r\n", length);
  if (chunkSizeLength <= 0 || static_cast<size_t>(chunkSizeLength) >= sizeof(chunkSize)) return false;

  const size_t headerLength = static_cast<size_t>(chunkSizeLength);
  if (client.write(reinterpret_cast<const uint8_t*>(chunkSize), headerLength) != headerLength ||
      client.write(reinterpret_cast<const uint8_t*>(bytes), length) != length ||
      client.write("\r\n", 2) != 2) {
    return false;
  }
  return client.connected();
}
