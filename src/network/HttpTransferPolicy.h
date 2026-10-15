#pragma once

#include <cstddef>

// Small, transport-independent rules used by HttpDownloader and its host tests.
// Socket I/O stays in the existing ESP/WolfSSL streaming loops.
namespace HttpTransferPolicy {
inline constexpr int MAX_REDIRECTS = 5;

inline bool isRedirectStatus(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

inline bool mayFollowRedirect(const int status, const int redirectsFollowed) {
  return isRedirectStatus(status) && redirectsFollowed < MAX_REDIRECTS;
}

inline bool isSuccessfulStatus(const int status) { return status == 200; }

inline bool isResponseComplete(const bool transportComplete, const size_t contentLength, const size_t received) {
  return transportComplete && (contentLength == 0 || contentLength == received);
}

inline bool hasDownloadBody(const size_t received) { return received > 0; }
}  // namespace HttpTransferPolicy
