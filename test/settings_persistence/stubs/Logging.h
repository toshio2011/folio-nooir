#pragma once

namespace testLogging {
template <typename... Args>
inline void discard(const Args&...) {}
}  // namespace testLogging

#define LOG_DBG(...) ::testLogging::discard(__VA_ARGS__)
#define LOG_INF(...) ::testLogging::discard(__VA_ARGS__)
#define LOG_ERR(...) ::testLogging::discard(__VA_ARGS__)
#define LOG_WRN(...) ::testLogging::discard(__VA_ARGS__)
