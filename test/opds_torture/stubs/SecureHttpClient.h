#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "util/UrlUtils.h"

namespace freeink {
class SecureHttpClient {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  using AbortCallback = std::function<bool()>;

  struct Response {
    int status = 200;
    std::string location;
    std::string body;
    bool hasLength = true;
    size_t declaredLength = static_cast<size_t>(-1);
    size_t chunkSize = 3;
    bool complete = true;
    bool transportError = false;
  };

  struct Request {
    std::string url;
    std::vector<std::string> headers;
    uint32_t timeoutMs = 0;
  };

  static void setResponses(std::vector<Response> values) {
    responses = std::move(values);
    requests.clear();
    responseIndex = 0;
    lastTimeoutMs = 0;
  }

  static void reset() { setResponses({}); }

  void setTimeout(const uint32_t timeout) { timeoutMs = timeout; }
  void setInsecure() {}
  void setUserAgent(const std::string& agent) { addHeader("User-Agent", agent); }
  void addHeader(const std::string& name, const std::string& value) { headers.push_back(name + ": " + value + "\r\n"); }

  bool begin(const std::string& value) {
    url = value;
    return !url.empty();
  }

  int GET(const DataCallback& onData, const AbortCallback& shouldAbort) {
    Request request{url, headers, timeoutMs};
    requests.push_back(request);
    lastTimeoutMs = timeoutMs;
    if (responseIndex >= responses.size()) return -1;
    const Response& response = responses[responseIndex++];
    status = response.transportError ? -1 : response.status;
    location = response.location;
    hasLengthValue = response.hasLength;
    length = response.declaredLength == static_cast<size_t>(-1) ? response.body.size() : response.declaredLength;
    complete = false;
    wasCallbackAborted = false;
    wasAborted = false;
    if (response.transportError) return -1;

    if (status == 200) {
      size_t offset = 0;
      const size_t chunkSize = response.chunkSize == 0 ? response.body.size() : response.chunkSize;
      while (offset < response.body.size()) {
        if (shouldAbort && shouldAbort()) {
          wasAborted = true;
          return status;
        }
        const size_t count = std::min(chunkSize, response.body.size() - offset);
        if (!onData(reinterpret_cast<const uint8_t*>(response.body.data() + offset), count)) {
          wasCallbackAborted = true;
          return status;
        }
        offset += count;
      }
      complete = response.complete;
    } else {
      complete = true;
    }
    return status;
  }

  int getStatus() const { return status; }
  bool hasContentLength() const { return hasLengthValue; }
  size_t getContentLength() const { return length; }
  bool responseComplete() const { return complete; }
  bool callbackAborted() const { return wasCallbackAborted; }
  bool aborted() const { return wasAborted; }
  std::string getHeader(const std::string& name) const {
    if (name == "location") return location;
    return {};
  }

  // The test transport only needs redirects resolved against ordinary catalog
  // URLs; production uses the same URL utility for OPDS navigation.
  static bool resolveUrl(const std::string& base, const std::string& reference, std::string& resolved) {
    if (base.empty() || reference.empty()) return false;
    resolved = UrlUtils::buildUrl(base, reference);
    return !resolved.empty();
  }

  inline static std::vector<Response> responses;
  inline static std::vector<Request> requests;
  inline static size_t responseIndex = 0;
  inline static uint32_t lastTimeoutMs = 0;

 private:
  std::string url;
  std::vector<std::string> headers;
  uint32_t timeoutMs = 0;
  int status = 0;
  bool hasLengthValue = false;
  size_t length = 0;
  bool complete = false;
  bool wasCallbackAborted = false;
  bool wasAborted = false;
  std::string location;
};
}  // namespace freeink
