#pragma once

#include <cstddef>
#include <cstdint>

namespace bleinput::iine_touch {

enum class Gesture : uint8_t { None = 0, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };
enum class PhysicalDirection : uint8_t { Unknown = 0, Left, Right, Up, Down };

// Touch Mode mirrors physical controller direction into the opposite
// touchscreen swipe direction. Keep the event identity unchanged for saved
// mappings; this helper is only for user-facing labels and diagnostics.
constexpr PhysicalDirection physicalDirectionForGesture(const Gesture gesture) {
  switch (gesture) {
    case Gesture::SwipeLeft: return PhysicalDirection::Right;
    case Gesture::SwipeRight: return PhysicalDirection::Left;
    case Gesture::SwipeUp: return PhysicalDirection::Down;
    case Gesture::SwipeDown: return PhysicalDirection::Up;
    default: return PhysicalDirection::Unknown;
  }
}

constexpr const char* physicalDirectionName(const PhysicalDirection direction) {
  switch (direction) {
    case PhysicalDirection::Left: return "Left";
    case PhysicalDirection::Right: return "Right";
    case PhysicalDirection::Up: return "Up";
    case PhysicalDirection::Down: return "Down";
    default: return "";
  }
}

// The fallback is deliberately specific to the captured IG05/IINE Touch Mode
// protocol: HID Report characteristic 0x2A4D, Report Reference ID 6/Input,
// Digitizer usage page, 20-byte report, status plus two 16-bit coordinates,
// and zero-filled reserved tail. It is never a generic mouse/touch decoder.
inline constexpr uint16_t kReportCharacteristic = 0x2A4D;
inline constexpr uint8_t kReportId = 0x06;
inline constexpr uint8_t kInputReportType = 0x01;
inline constexpr size_t kReportBytes = 20;
inline constexpr uint8_t kGestureIdentityKind = 4;
inline constexpr uint32_t kGestureTimeoutMs = 750;
inline constexpr uint32_t kMaxInterReportMs = 300;
inline constexpr uint16_t kTapMaxDisplacement = 300;
inline constexpr uint16_t kSwipeMinDisplacement = 600;

inline bool isSupportedDeviceName(const char* name) {
  // Both names were observed on the same IINE Touch Mode hardware family.
  // Callers still require the exact HID Report Reference and a descriptor
  // that assigns ID 6 to Digitizer coordinates before decoding a report.
  if (!name) return false;
  constexpr const char* kNames[] = {"IG05 Gamepad(BLE)", "IINE_control"};
  for (const char* expected : kNames) {
    size_t i = 0;
    for (; expected[i] && name[i] == expected[i]; ++i) {}
    if (expected[i] == '\0' && name[i] == '\0') return true;
  }
  return false;
}

inline bool hasSupportedContactSlots(const uint8_t* report, const size_t length) {
  if (!report || length != kReportBytes) return false;
  // Physical IG05 reports contain four five-byte contact slots. A second
  // active slot was observed during swipes; accept only that marker and
  // bounded coordinates. Unobserved slots remain required to be zero.
  const bool secondEmpty = report[5] == 0 && report[6] == 0 && report[7] == 0 &&
                           report[8] == 0 && report[9] == 0;
  if (!secondEmpty) {
    if (report[5] != 0x23) return false;
    const uint16_t x = static_cast<uint16_t>(report[6]) | (static_cast<uint16_t>(report[7]) << 8);
    const uint16_t y = static_cast<uint16_t>(report[8]) | (static_cast<uint16_t>(report[9]) << 8);
    if (x > 4095 || y > 4095) return false;
  }
  for (size_t i = 10; i < length; ++i) if (report[i] != 0) return false;
  return true;
}

inline bool isZeroCompanion(const uint8_t* report, const size_t length) {
  if (!report || length != 5) return false;
  return report[0] == 0 && report[1] == 0 && report[2] == 0 && report[3] == 0 && report[4] == 0;
}

// Validate the observed contact packet independently of a friendly device
// name. The caller must also match the Report characteristic and Report
// Reference metadata. This exact 20-byte grammar (contact state, bounded LE
// coordinates, up to two bounded contact slots, zero reserved tail) is the
// only descriptorless touch fallback; arbitrary mouse/gamepad reports do not
// enter the recognizer.
inline bool isValidatedContactReport(const uint8_t* report, const size_t length) {
  if (!hasSupportedContactSlots(report, length) || (report[0] != 0x13 && report[0] != 0x12)) return false;
  const uint16_t x = static_cast<uint16_t>(report[1]) | (static_cast<uint16_t>(report[2]) << 8);
  const uint16_t y = static_cast<uint16_t>(report[3]) | (static_cast<uint16_t>(report[4]) << 8);
  return x <= 4095 && y <= 4095;
}

class GestureRecognizer {
 public:
  Gesture consume(const uint8_t* report, const size_t length, const uint32_t nowMs) {
    // Companion notifications immediately following each 20-byte report are
    // observed as neutral five-byte reports. They neither update nor end a touch.
    if (isZeroCompanion(report, length)) return Gesture::None;

    if (!isValidatedContactReport(report, length)) {
      reset();
      return Gesture::None;
    }
    const uint16_t x = readLe16(report + 1);
    const uint16_t y = readLe16(report + 3);

    if (active_ && (static_cast<uint32_t>(nowMs - startedMs_) > kGestureTimeoutMs ||
                    static_cast<uint32_t>(nowMs - lastReportMs_) > kMaxInterReportMs)) {
      reset();
      return Gesture::None;  // Do not reinterpret a late continuation as a new tap.
    }

    if (report[0] == 0x13) {
      if (!active_) {
        active_ = true;
        startX_ = x;
        startY_ = y;
        startedMs_ = nowMs;
      }
      lastX_ = x;
      lastY_ = y;
      lastReportMs_ = nowMs;
      return Gesture::None;
    }

    // 0x12 is the captured contact release. A release without a current
    // contact, or one arriving after a stale gap, cannot synthesize an event.
    if (!active_) return Gesture::None;
    lastX_ = x;
    lastY_ = y;
    lastReportMs_ = nowMs;
    const Gesture result = classify(static_cast<int32_t>(lastX_) - startX_,
                                    static_cast<int32_t>(lastY_) - startY_);
    reset();
    return result;
  }

  void reset() {
    active_ = false;
    startX_ = startY_ = lastX_ = lastY_ = 0;
    startedMs_ = lastReportMs_ = 0;
  }

  bool active() const { return active_; }

 private:
  static uint16_t readLe16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
  }

  static uint32_t magnitude(const int32_t value) {
    return static_cast<uint32_t>(value < 0 ? -value : value);
  }

  static Gesture classify(const int32_t dx, const int32_t dy) {
    const uint32_t ax = magnitude(dx);
    const uint32_t ay = magnitude(dy);
    const uint32_t primary = ax > ay ? ax : ay;
    const uint32_t secondary = ax > ay ? ay : ax;
    if (primary <= kTapMaxDisplacement) return Gesture::Tap;
    if (primary < kSwipeMinDisplacement || primary < secondary + (secondary / 2)) return Gesture::None;
    if (ax > ay) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  }

  bool active_ = false;
  uint16_t startX_ = 0;
  uint16_t startY_ = 0;
  uint16_t lastX_ = 0;
  uint16_t lastY_ = 0;
  uint32_t startedMs_ = 0;
  uint32_t lastReportMs_ = 0;
};

template <typename Event, size_t Capacity>
bool enqueueGesturePair(Event (&ring)[Capacity], uint8_t& head, const uint8_t tail,
                        const Event& press, const Event& release) {
  static_assert(Capacity >= 3, "gesture queue must reserve empty/full distinction plus a pair");
  const size_t queued = (static_cast<size_t>(head) + Capacity - tail) % Capacity;
  const size_t free = Capacity - 1 - queued;
  if (free < 2) return false;
  ring[head] = press;
  head = static_cast<uint8_t>((head + 1) % Capacity);
  ring[head] = release;
  head = static_cast<uint8_t>((head + 1) % Capacity);
  return true;
}

}  // namespace bleinput::iine_touch
