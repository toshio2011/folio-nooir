#pragma once
#include <string>

namespace UrlUtils {

/**
 * Prepend http:// if no protocol specified (server will redirect to https if needed)
 */
std::string ensureProtocol(const std::string& url);

/**
 * Extract host with protocol from URL (e.g., "http://example.com" from "http://example.com/path")
 */
std::string extractHost(const std::string& url);

/**
 * Percent-encode raw characters that esp_http_client rejects in a URL.
 */
std::string encodeUnsafeUrlChars(const std::string& url);

/** Return whether two HTTP(S) URLs share the same scheme, host, and port. */
bool sameOrigin(const std::string& firstUrl, const std::string& secondUrl);

/** Return true when a redirect changes an HTTPS URL to plain HTTP. */
bool isHttpsDowngrade(const std::string& firstUrl, const std::string& secondUrl);

/**
 * Resolve a URL reference against a base URL and encode unsafe URL characters.
 * Supports absolute, root-relative, path-relative, query-only, and fragment
 * references used by OPDS feeds.
 */
std::string buildUrl(const std::string& serverUrl, const std::string& path);

}  // namespace UrlUtils
