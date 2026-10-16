#pragma once

namespace WebExitLifecyclePolicy {
struct Resources {
  bool httpServerStopped;
  bool wifiModeNull;
  bool stationNetifGone;
  bool accessPointNetifGone;
};

constexpr bool complete(const Resources& resources) {
  return resources.httpServerStopped && resources.wifiModeNull && resources.stationNetifGone &&
         resources.accessPointNetifGone;
}

// A completed teardown may be skipped on ActivityManager's later onExit call.
// If any resource is still present, the caller must run the cleanup path again.
constexpr bool maySkipCleanup(const bool prepared, const Resources& resources) {
  return prepared && complete(resources);
}
}  // namespace WebExitLifecyclePolicy
