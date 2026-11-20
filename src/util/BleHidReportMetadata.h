#pragma once

#include <cstddef>
#include <cstdint>

namespace bleinput::hid {

// 0xFF is a valid HID Report ID. This value is used only when the Report
// Reference descriptor is absent or unusable; callers must also check the
// hasReportReference flag before treating a byte as a Report ID.
inline constexpr uint8_t kUnknownReportId = 0xFF;
inline constexpr uint8_t kReportIdCapacity = 16;
inline constexpr uint8_t kGlobalPushDepth = 8;

struct PayloadView {
  const uint8_t* data = nullptr;
  size_t length = 0;
  bool reportIdMismatch = false;
};

struct ReportMapHints {
  bool available = false;
  bool valid = false;
  bool hasReportIds = false;
  bool overflow = false;
  bool hasKeyboardInput = false;
  bool hasConsumerInput = false;
  bool hasDigitizerInput = false;
  bool hasMouseInput = false;
  bool hasGamepadInput = false;
  bool hasUnsupportedUnnumberedInput = false;
  uint8_t keyboardIds[kReportIdCapacity] = {};
  uint8_t keyboardIdCount = 0;
  uint8_t consumerIds[kReportIdCapacity] = {};
  uint8_t consumerIdCount = 0;
  uint8_t digitizerIds[kReportIdCapacity] = {};
  uint8_t digitizerIdCount = 0;
  uint8_t unsupportedIds[kReportIdCapacity] = {};
  uint8_t unsupportedIdCount = 0;
  uint8_t otherUnsupportedIds[kReportIdCapacity] = {};
  uint8_t otherUnsupportedIdCount = 0;
  uint8_t digitizerCoordinateIds[kReportIdCapacity] = {};
  uint8_t digitizerCoordinateIdCount = 0;
  uint8_t digitizerCoordinateMasks[kReportIdCapacity] = {};
  uint8_t genericDesktopOtherIds[kReportIdCapacity] = {};
  uint8_t genericDesktopOtherIdCount = 0;
  uint8_t iineButtonMasks[kReportIdCapacity] = {};
  uint8_t iineButtonIds[kReportIdCapacity] = {};
  uint8_t iineButtonIdCount = 0;
  uint8_t preferredByteIndex = kUnknownReportId;
};

inline PayloadView payloadWithoutDeclaredReportId(const uint8_t* data, const size_t length,
                                                   const uint8_t reportId,
                                                   const bool hasReportReference) {
  if (!data || length == 0) return {};
  if (!hasReportReference || reportId == 0) return {data, length, false};
  if (data[0] != reportId) return {nullptr, 0, true};
  return {data + 1, length - 1, false};
}

// A HOGP Report Reference identifies the Report ID out-of-band. The GATT
// Report characteristic normally carries only the report body. When a valid
// Report Map gives us the exact body length, accept that form first; retain
// compatibility with stacks/devices that include the ID byte by stripping it
// only when the value is exactly one byte longer and the prefix matches.
// Without a usable Report Map, trust the Report Reference and treat the full
// characteristic value as body data instead of guessing from its first byte.
inline PayloadView payloadForDeclaredReport(const uint8_t* data, const size_t length,
                                             const uint8_t reportId, const bool hasReportReference,
                                             const bool hasExpectedBodyLength,
                                             const size_t expectedBodyLength) {
  if (!data || length == 0) return {};
  if (!hasReportReference || reportId == 0) return {data, length, false};
  if (!hasExpectedBodyLength) return {data, length, false};
  if (length == expectedBodyLength) return {data, length, false};
  if (length == expectedBodyLength + 1 && data[0] == reportId)
    return {data + 1, length - 1, false};
  return {nullptr, 0, true};
}

constexpr bool knownInputReportType(const uint8_t reportType, const bool hasReportReference) {
  return !hasReportReference || reportType == 1;
}

inline bool addReportId(uint8_t* ids, uint8_t& count, const uint8_t id, bool& overflow) {
  for (uint8_t index = 0; index < count; ++index) {
    if (ids[index] == id) return true;
  }
  if (count >= kReportIdCapacity) {
    overflow = true;
    return false;
  }
  ids[count++] = id;
  return true;
}

// Bounded HID short-item parser. It keeps only the input Report IDs and usage
// pages needed to route keyboard/consumer data safely; it is not a general HID
// field decoder. Malformed maps, oversized ID sets, and mixed-use IDs fail closed.
inline bool parseReportMap(const uint8_t* map, const size_t length, ReportMapHints& hints) {
  hints = ReportMapHints{};
  hints.available = true;
  if (!map || length == 0) return false;

  struct GlobalState {
    uint32_t usagePage;
    uint8_t reportId;
  };
  GlobalState stack[kGlobalPushDepth] = {};
  uint8_t depth = 0;
  uint32_t usagePage = 0;
  uint8_t reportId = 0;
  bool sawNumberedInput = false;
  bool sawUnnumberedInput = false;
  bool pendingMouseUsage = false;
  bool pendingGamepadUsage = false;
  uint8_t pendingCoordinateMask = 0;
  bool pendingGenericDesktopOtherUsage = false;
  uint8_t reportSize = 0;
  uint8_t reportCount = 0;
  uint8_t buttonUsageMinimum = 0;
  uint8_t buttonUsageMaximum = 0;
  size_t offset = 0;

  while (offset < length) {
    const uint8_t prefix = map[offset++];
    if (prefix == 0xFE) {
      if (length - offset < 2) return false;
      const size_t longLength = map[offset];
      offset += 2;  // Long-item length and tag.
      if (longLength > length - offset) return false;
      offset += longLength;
      continue;
    }

    const uint8_t sizeCode = prefix & 0x03;
    const size_t itemSize = sizeCode == 3 ? 4 : sizeCode;
    if (itemSize > length - offset) return false;
    uint32_t value = 0;
    for (size_t byte = 0; byte < itemSize; ++byte) {
      value |= static_cast<uint32_t>(map[offset + byte]) << (8 * byte);
    }
    offset += itemSize;
    const uint8_t itemType = (prefix >> 2) & 0x03;
    const uint8_t itemTag = prefix >> 4;

    if (itemType == 1) {  // Global items.
      if (itemTag == 0) {
        usagePage = value;
      } else if (itemTag == 7) {  // Report Size
        reportSize = value <= 0xFF ? static_cast<uint8_t>(value) : 0;
      } else if (itemTag == 9) {  // Report Count
        reportCount = value <= 0xFF ? static_cast<uint8_t>(value) : 0;
      } else if (itemTag == 8) {  // Report ID
        if (itemSize != 1 || value == 0 || value > 0xFF) return false;
        reportId = static_cast<uint8_t>(value);
        hints.hasReportIds = true;
      } else if (itemTag == 10) {  // Push
        if (itemSize != 0 || depth >= kGlobalPushDepth) return false;
        stack[depth++] = {usagePage, reportId};
      } else if (itemTag == 11) {  // Pop
        if (itemSize != 0 || depth == 0) return false;
        const GlobalState restored = stack[--depth];
        usagePage = restored.usagePage;
        reportId = restored.reportId;
      }
      continue;
    }

    if (itemType == 2 && itemTag == 0) {  // Local Usage
      if (usagePage == 0x01 && value == 0x02) pendingMouseUsage = true;
      if (usagePage == 0x01 && (value == 0x04 || value == 0x05)) pendingGamepadUsage = true;
      if (usagePage == 0x01) {
        if (value == 0x30) pendingCoordinateMask |= 0x01;
        else if (value == 0x31) pendingCoordinateMask |= 0x02;
        else pendingGenericDesktopOtherUsage = true;
      }
      continue;
    }
    if (itemType == 2 && itemTag == 1 && usagePage == 0x09) {  // Usage Minimum
      buttonUsageMinimum = value <= 0xFF ? static_cast<uint8_t>(value) : 0;
      continue;
    }
    if (itemType == 2 && itemTag == 2 && usagePage == 0x09) {  // Usage Maximum
      buttonUsageMaximum = value <= 0xFF ? static_cast<uint8_t>(value) : 0;
      continue;
    }

    if (itemType != 0 || itemTag != 8) continue;  // Only Main Input items describe input reports.

    if (reportId == 0) sawUnnumberedInput = true;
    else sawNumberedInput = true;

    if (usagePage == 0x07) {
      hints.hasKeyboardInput = true;
      addReportId(hints.keyboardIds, hints.keyboardIdCount, reportId, hints.overflow);
    } else if (usagePage == 0x0C) {
      hints.hasConsumerInput = true;
      addReportId(hints.consumerIds, hints.consumerIdCount, reportId, hints.overflow);
    } else {
      if (reportId == 0) {
        hints.hasUnsupportedUnnumberedInput = true;
      } else {
        addReportId(hints.unsupportedIds, hints.unsupportedIdCount, reportId, hints.overflow);
        if (usagePage == 0x0D) {
          addReportId(hints.digitizerIds, hints.digitizerIdCount, reportId, hints.overflow);
        } else if (usagePage == 0x01) {
          if (pendingCoordinateMask) {
            uint8_t index = 0;
            while (index < hints.digitizerCoordinateIdCount && hints.digitizerCoordinateIds[index] != reportId)
              ++index;
            if (index == hints.digitizerCoordinateIdCount &&
                addReportId(hints.digitizerCoordinateIds, hints.digitizerCoordinateIdCount, reportId, hints.overflow)) {
              hints.digitizerCoordinateMasks[index] = 0;
            }
            if (index < hints.digitizerCoordinateIdCount)
              hints.digitizerCoordinateMasks[index] |= pendingCoordinateMask;
          }
          if (pendingGenericDesktopOtherUsage || !pendingCoordinateMask)
            addReportId(hints.genericDesktopOtherIds, hints.genericDesktopOtherIdCount, reportId, hints.overflow);
        } else if (usagePage == 0x09 && (value & 0x01u) == 0) {
          const uint8_t width = buttonUsageMaximum >= buttonUsageMinimum
                                    ? static_cast<uint8_t>(buttonUsageMaximum - buttonUsageMinimum + 1)
                                    : 0;
          if (buttonUsageMinimum == 1 && buttonUsageMaximum == 5 && reportSize == 1 && reportCount == 5 &&
              width == 5) {
            int8_t buttonIndex = -1;
            for (uint8_t index = 0; index < hints.iineButtonIdCount; ++index) {
              if (hints.iineButtonIds[index] == reportId) buttonIndex = static_cast<int8_t>(index);
            }
            if (buttonIndex < 0 && hints.iineButtonIdCount < kReportIdCapacity) {
              buttonIndex = static_cast<int8_t>(hints.iineButtonIdCount);
              hints.iineButtonIds[hints.iineButtonIdCount++] = reportId;
            }
            if (buttonIndex >= 0) hints.iineButtonMasks[buttonIndex] |= 0x1F;
          } else {
            addReportId(hints.otherUnsupportedIds, hints.otherUnsupportedIdCount, reportId, hints.overflow);
          }
        } else if (usagePage != 0x09) {
          addReportId(hints.otherUnsupportedIds, hints.otherUnsupportedIdCount, reportId, hints.overflow);
        }
      }
      if (usagePage == 0x0D) hints.hasDigitizerInput = true;
      if (pendingMouseUsage) hints.hasMouseInput = true;
      if (pendingGamepadUsage) hints.hasGamepadInput = true;
    }
    pendingMouseUsage = false;
    pendingGamepadUsage = false;
    pendingCoordinateMask = 0;
    pendingGenericDesktopOtherUsage = false;
    buttonUsageMinimum = 0;
    buttonUsageMaximum = 0;
  }

  if (depth != 0 || (sawNumberedInput && sawUnnumberedInput) || hints.overflow) return false;
  hints.valid = true;
  hints.preferredByteIndex = hints.hasKeyboardInput ? 2 : hints.hasConsumerInput ? 1 : kUnknownReportId;
  return true;
}

inline bool reportIdHasUsagePage(const uint8_t reportId, const uint8_t usagePage,
                                 const ReportMapHints& hints) {
  if (usagePage == 0x07) {
    for (uint8_t index = 0; index < hints.keyboardIdCount; ++index) {
      if (hints.keyboardIds[index] == reportId) return true;
    }
  } else if (usagePage == 0x0C) {
    for (uint8_t index = 0; index < hints.consumerIdCount; ++index) {
      if (hints.consumerIds[index] == reportId) return true;
    }
  }
  return false;
}

inline bool reportIdHasUnsupportedUsage(const uint8_t reportId, const ReportMapHints& hints) {
  for (uint8_t index = 0; index < hints.unsupportedIdCount; ++index) {
    if (hints.unsupportedIds[index] == reportId) return true;
  }
  return false;
}

inline bool reportIdHasDigitizerUsage(const uint8_t reportId, const ReportMapHints& hints) {
  for (uint8_t index = 0; index < hints.digitizerIdCount; ++index) {
    if (hints.digitizerIds[index] == reportId) return true;
  }
  return false;
}

inline bool reportIdHasOtherUnsupportedUsage(const uint8_t reportId, const ReportMapHints& hints) {
  for (uint8_t index = 0; index < hints.otherUnsupportedIdCount; ++index) {
    if (hints.otherUnsupportedIds[index] == reportId) return true;
  }
  return false;
}

inline bool reportIdHasId(const uint8_t reportId, const uint8_t* ids, const uint8_t count) {
  for (uint8_t index = 0; index < count; ++index) if (ids[index] == reportId) return true;
  return false;
}

inline bool reportIdHasDigitizerCoordinates(const uint8_t reportId, const ReportMapHints& hints) {
  for (uint8_t index = 0; index < hints.digitizerCoordinateIdCount; ++index) {
    if (hints.digitizerCoordinateIds[index] == reportId)
      return hints.digitizerCoordinateMasks[index] == 0x03;
  }
  return false;
}

inline bool reportIdHasOtherGenericDesktopUsage(const uint8_t reportId, const ReportMapHints& hints) {
  return reportIdHasId(reportId, hints.genericDesktopOtherIds, hints.genericDesktopOtherIdCount);
}

inline uint8_t reportIdIineButtonMask(const uint8_t reportId, const ReportMapHints& hints) {
  for (uint8_t index = 0; index < hints.iineButtonIdCount; ++index) {
    if (hints.iineButtonIds[index] == reportId) return hints.iineButtonMasks[index];
  }
  return 0;
}

// Touch decoding is a separate, opt-in route. A numbered report is eligible
// only when the complete parsed map assigns that ID to Digitizer input and no
// keyboard, Consumer Control, or other unsupported page shares the ID.
inline bool reportMapAllowsDigitizerInput(const ReportMapHints& hints, const uint8_t reportId,
                                          const bool hasReportReference, const uint8_t reportType) {
  return hints.available && hints.valid && !hints.overflow && hints.hasReportIds &&
         hasReportReference && reportId != 0 && knownInputReportType(reportType, true) &&
         reportIdHasDigitizerUsage(reportId, hints) &&
         reportIdIineButtonMask(reportId, hints) == 0 &&
         !reportIdHasUsagePage(reportId, 0x07, hints) &&
         !reportIdHasUsagePage(reportId, 0x0C, hints) &&
         !reportIdHasOtherUnsupportedUsage(reportId, hints);
}

// IG05 Touch Mode declares Digitizer contact data and X/Y coordinates in the
// same numbered report. Keep this exception strict: require Digitizer and X/Y,
// and reject other Generic Desktop fields or any other mixed usage page.
inline bool reportMapAllowsDigitizerCoordinates(const ReportMapHints& hints, const uint8_t reportId,
                                                const bool hasReportReference, const uint8_t reportType) {
  return hints.available && hints.valid && !hints.overflow && hints.hasReportIds &&
         hasReportReference && reportId != 0 && knownInputReportType(reportType, true) &&
         reportIdHasDigitizerUsage(reportId, hints) && reportIdHasDigitizerCoordinates(reportId, hints) &&
         reportIdIineButtonMask(reportId, hints) == 0 &&
         !reportIdHasOtherGenericDesktopUsage(reportId, hints) &&
         !reportIdHasUsagePage(reportId, 0x07, hints) &&
         !reportIdHasUsagePage(reportId, 0x0C, hints) &&
         !reportIdHasOtherUnsupportedUsage(reportId, hints);
}

inline bool reportMapAllowsUsage(const ReportMapHints& hints, const uint8_t reportId,
                                 const bool hasReportReference, const uint8_t reportType,
                                 const uint8_t usagePage) {
  if (!hints.available || !hints.valid || hints.overflow ||
      !knownInputReportType(reportType, hasReportReference)) return false;
  if (hints.hasReportIds) {
    if (!hasReportReference || reportId == 0 || reportIdHasUnsupportedUsage(reportId, hints) ||
        reportIdIineButtonMask(reportId, hints) != 0 ||
        reportIdHasDigitizerUsage(reportId, hints)) return false;
    const bool keyboard = reportIdHasUsagePage(reportId, 0x07, hints);
    const bool consumer = reportIdHasUsagePage(reportId, 0x0C, hints);
    if (keyboard && consumer) return false;
    return usagePage == 0x07 ? keyboard : usagePage == 0x0C ? consumer : false;
  }
  if (hasReportReference && reportId != 0) return false;
  if (hints.hasUnsupportedUnnumberedInput) return false;
  return usagePage == 0x07 ? hints.hasKeyboardInput
                           : usagePage == 0x0C ? hints.hasConsumerInput : false;
}

// IINE IG05 Report ID 6 has descriptor-declared Digitizer contact data plus
// X/Y coordinates, but firmware revisions may include additional bounded
// fields in that same numbered report. The device-specific caller must still
// validate the exact advertised identity and the 20-byte contact grammar.
// This predicate only establishes that ID 6 really contains both usages and
// is not shared with keyboard, Consumer Control, or the separately decoded
// button report.
inline bool reportMapAllowsDeviceScopedDigitizerCoordinates(const ReportMapHints& hints, const uint8_t reportId,
                                                             const bool hasReportReference,
                                                             const uint8_t reportType) {
  return hints.available && hints.valid && !hints.overflow && hints.hasReportIds && hasReportReference &&
         reportId != 0 && knownInputReportType(reportType, true) && reportIdHasDigitizerUsage(reportId, hints) &&
         reportIdHasDigitizerCoordinates(reportId, hints) && reportIdIineButtonMask(reportId, hints) == 0 &&
         !reportIdHasUsagePage(reportId, 0x07, hints) && !reportIdHasUsagePage(reportId, 0x0C, hints);
}

inline bool reportMapAllowsIineFiveButtonReport(const ReportMapHints& hints, const uint8_t reportId,
                                                const bool hasReportReference, const uint8_t reportType) {
  return hints.available && hints.valid && !hints.overflow && hints.hasReportIds && hasReportReference &&
         reportId != 0 && knownInputReportType(reportType, true) &&
         reportIdIineButtonMask(reportId, hints) == 0x1F && !reportIdHasDigitizerUsage(reportId, hints) &&
         !reportIdHasUsagePage(reportId, 0x07, hints) && !reportIdHasUsagePage(reportId, 0x0C, hints) &&
         !reportIdHasOtherUnsupportedUsage(reportId, hints);
}

// Composite descriptor signature seen on the IINE Touch Mode controller: five
// Button usages on ID 4 plus Digitizer contact and both coordinate axes on ID
// 6. This permits a narrowly validated descriptor-scoped fallback when the
// peripheral's friendly name differs across firmware revisions.
inline bool reportMapMatchesIineTouchComposite(const ReportMapHints& hints) {
  return reportMapAllowsIineFiveButtonReport(hints, 4, true, 1) &&
         reportMapAllowsDeviceScopedDigitizerCoordinates(hints, 6, true, 1);
}

inline bool genericInputReportAllowed(const ReportMapHints& hints, const uint8_t reportId,
                                      const bool hasReportReference, const uint8_t reportType) {
  // Preserve the legacy short-report decoder only when there was no readable
  // Report Map. With a descriptor, decode only a uniquely declared Consumer ID.
  if (!hints.available) return knownInputReportType(reportType, hasReportReference);
  return reportMapAllowsUsage(hints, reportId, hasReportReference, reportType, 0x0C);
}

}  // namespace bleinput::hid
