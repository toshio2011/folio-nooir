#include "UrlUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <string_view>
#include <vector>

namespace UrlUtils {
namespace {
bool isHexDigit(const char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

bool shouldEncode(const unsigned char c) {
  if (c <= 0x20 || c >= 0x7f) return true;
  switch (c) {
    case '"':
    case '<':
    case '>':
    case '\\':
    case '^':
    case '`':
    case '{':
    case '|':
    case '}':
      return true;
    default:
      return false;
  }
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool isAbsoluteAuthorityReference(const std::string& reference) {
  const size_t schemeEnd = reference.find("://");
  if (schemeEnd == std::string::npos || schemeEnd == 0 ||
      !std::isalpha(static_cast<unsigned char>(reference.front()))) {
    return false;
  }
  for (size_t i = 1; i < schemeEnd; ++i) {
    const unsigned char c = static_cast<unsigned char>(reference[i]);
    if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return false;
  }
  return true;
}

struct UrlParts {
  std::string scheme;
  std::string authority;
  std::string path;
  std::string query;
  std::string fragment;
  bool hasQuery = false;
  bool hasFragment = false;
};

bool splitAbsoluteUrl(const std::string& url, UrlParts& parts) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos || schemeEnd == 0) return false;
  const size_t authorityStart = schemeEnd + 3;
  const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  if (authorityStart == (authorityEnd == std::string::npos ? url.size() : authorityEnd)) return false;

  parts = UrlParts{};
  parts.scheme = url.substr(0, schemeEnd);
  parts.authority = url.substr(authorityStart, authorityEnd == std::string::npos ? std::string::npos
                                                                                : authorityEnd - authorityStart);

  std::string_view tail = authorityEnd == std::string::npos ? std::string_view{} : std::string_view(url).substr(authorityEnd);
  const size_t fragmentPos = tail.find('#');
  if (fragmentPos != std::string_view::npos) {
    parts.hasFragment = true;
    parts.fragment.assign(tail.substr(fragmentPos + 1));
    tail = tail.substr(0, fragmentPos);
  }
  const size_t queryPos = tail.find('?');
  if (queryPos != std::string_view::npos) {
    parts.hasQuery = true;
    parts.query.assign(tail.substr(queryPos + 1));
    tail = tail.substr(0, queryPos);
  }
  parts.path.assign(tail);
  return true;
}

std::string normalizePath(const std::string& path) {
  std::vector<std::string> segments;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    const std::string segment = path.substr(start, end - start);
    if (segment == "..") {
      if (!segments.empty() && !(segments.size() == 1 && segments[0].empty())) segments.pop_back();
    } else if (segment != ".") {
      segments.push_back(segment);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }

  std::string normalized;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i != 0) normalized += '/';
    normalized += segments[i];
  }
  if ((normalized.empty() || (!path.empty() && path.front() == '/')) &&
      (normalized.empty() || normalized.front() != '/')) {
    normalized.insert(normalized.begin(), '/');
  }
  if (!path.empty() && (path.back() == '/' || path.substr(path.size() >= 2 ? path.size() - 2 : 0) == "/." ||
                        path.substr(path.size() >= 3 ? path.size() - 3 : 0) == "/..") &&
      normalized.back() != '/') {
    normalized += '/';
  }
  return normalized;
}

bool parseOrigin(const std::string& url, std::string& scheme, std::string& host, unsigned& port) {
  UrlParts parts;
  if (!splitAbsoluteUrl(url, parts)) return false;
  scheme = lowerAscii(parts.scheme);
  if (scheme != "http" && scheme != "https") return false;

  std::string authority = parts.authority;
  const size_t userInfo = authority.rfind('@');
  if (userInfo != std::string::npos) authority.erase(0, userInfo + 1);

  std::string portText;
  if (!authority.empty() && authority.front() == '[') {
    const size_t bracket = authority.find(']');
    if (bracket == std::string::npos) return false;
    host = lowerAscii(authority.substr(0, bracket + 1));
    const std::string suffix = authority.substr(bracket + 1);
    if (!suffix.empty()) {
      if (suffix.front() != ':') return false;
      portText = suffix.substr(1);
      if (portText.empty()) return false;
    }
  } else {
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      if (authority.find(':') != colon) return false;
      host = lowerAscii(authority.substr(0, colon));
      portText = authority.substr(colon + 1);
      if (portText.empty()) return false;
    } else {
      host = lowerAscii(authority);
    }
  }
  if (host.empty()) return false;

  if (portText.empty()) {
    port = scheme == "https" ? 443u : 80u;
    return true;
  }
  unsigned parsedPort = 0;
  for (const unsigned char c : portText) {
    if (!std::isdigit(c)) return false;
    const unsigned digit = static_cast<unsigned>(c - '0');
    const unsigned maxPort = std::numeric_limits<unsigned short>::max();
    if (parsedPort > (maxPort - digit) / 10u) return false;
    parsedPort = parsedPort * 10u + digit;
  }
  if (parsedPort == 0) return false;
  port = parsedPort;
  return true;
}
}  // namespace

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    // No protocol, find first slash
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  // Find the first slash after the protocol
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string encodeUnsafeUrlChars(const std::string& url) {
  std::string out;
  out.reserve(url.size());
  for (size_t i = 0; i < url.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c == '%' && i + 2 < url.size() && isHexDigit(url[i + 1]) && isHexDigit(url[i + 2])) {
      out += url[i];
      out += url[i + 1];
      out += url[i + 2];
      i += 2;
    } else if (c == '%' || shouldEncode(c)) {
      char encoded[4];
      snprintf(encoded, sizeof(encoded), "%%%02X", c);
      out += encoded;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

bool sameOrigin(const std::string& firstUrl, const std::string& secondUrl) {
  std::string firstScheme;
  std::string firstHost;
  unsigned firstPort = 0;
  std::string secondScheme;
  std::string secondHost;
  unsigned secondPort = 0;
  return parseOrigin(firstUrl, firstScheme, firstHost, firstPort) &&
         parseOrigin(secondUrl, secondScheme, secondHost, secondPort) && firstScheme == secondScheme &&
         firstHost == secondHost && firstPort == secondPort;
}

bool isHttpsDowngrade(const std::string& firstUrl, const std::string& secondUrl) {
  UrlParts first;
  UrlParts second;
  return splitAbsoluteUrl(firstUrl, first) && splitAbsoluteUrl(secondUrl, second) &&
         lowerAscii(first.scheme) == "https" && lowerAscii(second.scheme) == "http";
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  // Absolute references replace the base URL. Network-path references keep
  // the base scheme while changing authority.
  if (isAbsoluteAuthorityReference(path)) {
    return encodeUnsafeUrlChars(path);
  }
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  UrlParts base;
  if (!splitAbsoluteUrl(urlWithProtocol, base)) return encodeUnsafeUrlChars(urlWithProtocol + path);

  std::string reference = path;
  if (reference.rfind("//", 0) == 0) return encodeUnsafeUrlChars(base.scheme + ":" + reference);

  const size_t fragmentPos = reference.find('#');
  const bool hasFragment = fragmentPos != std::string::npos;
  const std::string fragment = hasFragment ? reference.substr(fragmentPos + 1) : std::string{};
  if (hasFragment) reference.resize(fragmentPos);
  const size_t queryPos = reference.find('?');
  const bool hasQuery = queryPos != std::string::npos;
  const std::string query = hasQuery ? reference.substr(queryPos + 1) : std::string{};
  if (hasQuery) reference.resize(queryPos);

  std::string resolvedPath;
  std::string resolvedQuery;
  if (reference.empty()) {
    resolvedPath = base.path.empty() ? "/" : base.path;
    resolvedQuery = hasQuery ? query : base.query;
  } else {
    if (reference.front() == '/') {
      resolvedPath = reference;
    } else {
      std::string baseDirectory = base.path;
      if (baseDirectory.empty()) baseDirectory = "/";
      const size_t lastSlash = baseDirectory.rfind('/');
      baseDirectory.resize(lastSlash == std::string::npos ? 0 : lastSlash + 1);
      resolvedPath = baseDirectory + reference;
    }
    resolvedQuery = hasQuery ? query : std::string{};
  }
  resolvedPath = normalizePath(resolvedPath);

  std::string resolved = base.scheme + "://" + base.authority + resolvedPath;
  if (hasQuery || (reference.empty() && base.hasQuery)) resolved += "?" + resolvedQuery;
  if (hasFragment) resolved += "#" + fragment;
  return encodeUnsafeUrlChars(resolved);
}

}  // namespace UrlUtils
