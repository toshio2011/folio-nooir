#pragma once

#include <cstddef>
#include <cstdint>

namespace bleinput::iine_touch {

// The physical IG05 Report Map declares Report ID 4 with five 1-bit Button
// usages, three constant padding bits, X/Y and Wheel. Its six-byte payload is
// handled only after the caller validates the exact device, characteristic,
// Report Reference and parsed descriptor.
inline constexpr uint8_t kButtonReportId = 0x04;
inline constexpr size_t kButtonReportBytes = 6;
inline constexpr uint8_t kButtonCount = 5;
inline constexpr uint8_t kButtonMask = 0x1F;
inline constexpr uint8_t kButtonPaddingMask = 0xE0;

struct ButtonEdges {
  bool valid = false;
  uint8_t pressed = 0;
  uint8_t released = 0;
};

class ButtonDecoder {
 public:
  ButtonEdges consume(const uint8_t* report, const size_t length) {
    if (!report) {
      const ButtonEdges invalid{false, 0, heldMask_};
      heldMask_ = 0;
      return invalid;
    }
    // Some HID stacks include the declared Report ID in notifications while
    // this controller's captured Touch reports omit it. Accept either form,
    // but only strip byte 0 when length and declared ID both agree.
    const uint8_t* payload = report;
    size_t payloadLength = length;
    if (length == kButtonReportBytes + 1 && report[0] == kButtonReportId) {
      payload++;
      payloadLength--;
    }
    if (payloadLength != kButtonReportBytes || (payload[0] & kButtonPaddingMask) != 0) {
      const ButtonEdges invalid{false, 0, heldMask_};
      heldMask_ = 0;
      return invalid;
    }
    const uint8_t next = payload[0] & kButtonMask;
    const ButtonEdges edges{true, static_cast<uint8_t>(next & ~heldMask_),
                            static_cast<uint8_t>(heldMask_ & ~next)};
    heldMask_ = next;
    return edges;
  }

  ButtonEdges reset() {
    const ButtonEdges released{true, 0, heldMask_};
    heldMask_ = 0;
    return released;
  }

  uint8_t heldMask() const { return heldMask_; }

 private:
  uint8_t heldMask_ = 0;
};

}  // namespace bleinput::iine_touch
