#pragma once

namespace TransientNotificationPolicy {
// Global toasts are not part of an activity's modal UI. Keep them out of
// reader frames and modal popups; queued messages can be shown after the modal
// closes if their normal lifetime has not expired.
constexpr bool canPaint(const bool readerActive, const bool modalOverlayActive) {
  return !readerActive && !modalOverlayActive;
}
}  // namespace TransientNotificationPolicy
