#pragma once

#include <cstddef>

namespace reader_build_policy {

// A foreground render cannot show anything until page zero exists. Once the
// heap gate has already suspended and reclaimed a zero-page build, retrying
// the same parse under the same safety floor cannot make progress. Sections
// with cached pages remain resumable, including optional background builds.
constexpr bool shouldShowFirstPageHeapError(const size_t readablePages,
                                           const bool suspendedAfterHeapRecovery) noexcept {
  return suspendedAfterHeapRecovery && readablePages == 0;
}

}  // namespace reader_build_policy
