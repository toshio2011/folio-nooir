#pragma once

#include <cstddef>
#include <cstdint>

// Bounded, descriptor-driven input learning for HID controls which do not fit
// the keyboard/Consumer paths. It learns descriptor-declared one-bit fields,
// null-state hats, gamepad/joystick axes, and release-time gestures only from a
// declared Touch Screen/Touch Pad contact plus X/Y report. Coordinates, arrays,
// constants, counters, and unknown multi-bit fields never become identities.
namespace bleinput::hid::learning {

constexpr uint8_t kMaxFields = 32;
constexpr uint8_t kMaxReports = 16;
constexpr uint8_t kMaxGlobalDepth = 8;
constexpr uint8_t kDescriptorHashParts = 4;
constexpr uint8_t kIdentityParts = 7;
constexpr uint8_t kMaxEdges = 16;
constexpr uint8_t kRawFallbackMaxBytes = 16;
constexpr uint32_t kRawFallbackMaxGapMs = 1000;

enum class FieldKind : uint8_t { ButtonBit, Hat, Axis, TouchContact, TouchX, TouchY };
constexpr uint32_t kTouchMaxDurationMs = 1500;
constexpr uint32_t kTouchMaxInterReportMs = 300;

struct Field {
  uint16_t bitOffset = 0;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
  uint16_t usagePage = 0;
  uint16_t usage = 0;
  uint8_t reportId = 0;
  uint8_t token = 0;
  uint8_t bitSize = 0;
  uint8_t flags = 0;
  FieldKind kind = FieldKind::ButtonBit;
};

struct Report {
  uint16_t inputBits = 0;
  uint8_t id = 0;
  uint8_t fieldCount = 0;
  bool hasKeyboardOrConsumer = false;
};

struct Layout {
  Field fields[kMaxFields] = {};
  Report reports[kMaxReports] = {};
  uint32_t descriptorHash = 2166136261u;
  uint8_t fieldCount = 0;
  uint8_t reportCount = 0;
  bool valid = false;
  bool hasReportIds = false;
  bool overflow = false;
};

inline bool expectedInputBytes(const Layout& layout, const uint8_t reportId, size_t& bytes) {
  bytes = 0;
  if (!layout.valid) return false;
  for (uint8_t i = 0; i < layout.reportCount; ++i) {
    if (layout.reports[i].id != reportId || layout.reports[i].inputBits == 0) continue;
    bytes = (static_cast<size_t>(layout.reports[i].inputBits) + 7u) / 8u;
    return bytes != 0;
  }
  return false;
}

inline bool reportHasTouchGestureFields(const Layout& layout, const uint8_t reportId) {
  bool contact = false, x = false, y = false;
  if (!layout.valid) return false;
  for (uint8_t i = 0; i < layout.fieldCount; ++i) {
    const Field& field = layout.fields[i];
    if (field.reportId != reportId) continue;
    contact |= field.kind == FieldKind::TouchContact;
    x |= field.kind == FieldKind::TouchX;
    y |= field.kind == FieldKind::TouchY;
  }
  return contact && x && y;
}

struct Edge {
  uint8_t fieldToken = 0;
  uint8_t state = 0;  // 1=button, 1..8=hat direction, 1=negative/2=positive axis.
  bool pressed = false;
};

struct IdentityPart {
  uint8_t kind = 0xFF;
  uint8_t value = 0;
};

struct RawBitFallback {
  uint8_t previous[kRawFallbackMaxBytes] = {};
  uint32_t identity = 0;
  uint32_t lastMs = 0;
  uint8_t length = 0;
  uint8_t activeBit = 0xFF;
  bool initialized = false;

  void reset() { *this = RawBitFallback{}; }
};

struct Decoder {
  struct TouchState {
    int32_t startX = 0, startY = 0, lastX = 0, lastY = 0;
    uint32_t startedMs = 0, lastMs = 0;
    bool initialized = false;
    bool contact = false;
  };
  uint8_t buttonState[kMaxFields] = {};
  uint8_t hatState[kMaxFields] = {};
  int8_t axisState[kMaxFields] = {};
  bool initialized[kMaxFields] = {};
  TouchState touch[kMaxReports] = {};

  void reset() {
    for (uint8_t i = 0; i < kMaxFields; ++i) {
      buttonState[i] = 0;
      hatState[i] = 0;
      axisState[i] = 0;
      initialized[i] = false;
    }
    for (uint8_t i = 0; i < kMaxReports; ++i) touch[i] = TouchState{};
  }

  void resetTouch() {
    for (uint8_t i = 0; i < kMaxReports; ++i) touch[i] = TouchState{};
  }
};

inline uint32_t fnv1a(const uint8_t* bytes, const size_t length) {
  uint32_t hash = 2166136261u;
  if (!bytes) return hash;
  for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

inline int32_t signedItemValue(const uint32_t value, const size_t itemSize) {
  if (itemSize == 0) return 0;
  if (itemSize >= 4) return static_cast<int32_t>(value);
  const uint8_t bits = static_cast<uint8_t>(itemSize * 8);
  const uint32_t sign = 1u << (bits - 1);
  return (value & sign) ? static_cast<int32_t>(value | (~0u << bits))
                        : static_cast<int32_t>(value);
}

inline bool identityParts(const uint32_t descriptorHash, const uint8_t reportId,
                          const uint8_t fieldToken, const uint8_t state,
                          IdentityPart* parts, const uint8_t capacity) {
  if (!parts || capacity < kIdentityParts || state == 0) return false;
  parts[0] = {6, static_cast<uint8_t>(descriptorHash)};
  parts[1] = {7, static_cast<uint8_t>(descriptorHash >> 8)};
  parts[2] = {8, static_cast<uint8_t>(descriptorHash >> 16)};
  parts[3] = {9, static_cast<uint8_t>(descriptorHash >> 24)};
  parts[4] = {10, reportId};
  parts[5] = {11, fieldToken};
  parts[6] = {12, state};
  return true;
}

inline bool addField(Layout& layout, const uint8_t reportId, const uint16_t bitOffset,
                     const uint8_t bitSize, const uint16_t usagePage, const uint16_t usage,
                     const int32_t logicalMin, const int32_t logicalMax, const uint8_t flags,
                     const FieldKind kind) {
  if (layout.fieldCount >= kMaxFields || layout.fieldCount >= 0xFF) {
    layout.overflow = true;
    return false;
  }
  Field& field = layout.fields[layout.fieldCount];
  field.bitOffset = bitOffset;
  field.reportId = reportId;
  field.token = layout.fieldCount;
  field.bitSize = bitSize;
  field.usagePage = usagePage;
  field.usage = usage;
  field.logicalMin = logicalMin;
  field.logicalMax = logicalMax;
  field.flags = flags;
  field.kind = kind;
  ++layout.fieldCount;
  for (uint8_t i = 0; i < layout.reportCount; ++i) {
    if (layout.reports[i].id == reportId) {
      ++layout.reports[i].fieldCount;
      break;
    }
  }
  return true;
}

inline Report* findReport(Layout& layout, const uint8_t id) {
  for (uint8_t i = 0; i < layout.reportCount; ++i) {
    if (layout.reports[i].id == id) return &layout.reports[i];
  }
  if (layout.reportCount >= kMaxReports) {
    layout.overflow = true;
    return nullptr;
  }
  Report& report = layout.reports[layout.reportCount++];
  report = Report{};
  report.id = id;
  return &report;
}

// Parse just the stable control fields needed for learning. This is not a
// replacement for the stricter report-routing parser in BleHidReportMetadata.
inline bool parseLayout(const uint8_t* map, const size_t length, Layout& layout) {
  layout = Layout{};
  if (!map || length == 0 || length > 1024) return false;

  struct Global {
    uint32_t usagePage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint32_t reportSize = 0;
    uint32_t reportCount = 0;
    uint8_t reportId = 0;
  };
  struct Local {
    uint16_t usages[16] = {};
    uint8_t count = 0;
    uint16_t minimum = 0;
    uint16_t maximum = 0;
    bool hasRange = false;
    void clear() { *this = Local{}; }
  };
  Global globals[kMaxGlobalDepth] = {};
  bool appGamepadOrJoystick = false;
  bool appTouch = false;
  bool collectionStack[kMaxGlobalDepth] = {};
  bool touchCollectionStack[kMaxGlobalDepth] = {};
  uint8_t globalDepth = 0;
  uint8_t collectionDepth = 0;
  Global global{};
  Local local{};
  size_t offset = 0;

  layout.descriptorHash = fnv1a(map, length);
  while (offset < length) {
    const uint8_t prefix = map[offset++];
    if (prefix == 0xFE) {
      if (length - offset < 2) return false;
      const size_t longLength = map[offset];
      offset += 2;
      if (longLength > length - offset) return false;
      offset += longLength;
      continue;
    }
    const uint8_t sizeCode = prefix & 0x03;
    const size_t itemSize = sizeCode == 3 ? 4 : sizeCode;
    if (itemSize > length - offset) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < itemSize; ++i) value |= static_cast<uint32_t>(map[offset + i]) << (8 * i);
    offset += itemSize;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag = prefix >> 4;

    if (type == 1) {  // Global
      switch (tag) {
        case 0: global.usagePage = value; break;
        case 1: global.logicalMin = signedItemValue(value, itemSize); break;
        case 2:
          global.logicalMax = global.logicalMin < 0 ? signedItemValue(value, itemSize)
                                                     : static_cast<int32_t>(value);
          break;
        case 7: global.reportSize = value; break;
        case 8:
          if (itemSize != 1 || value == 0 || value > 0xFF) return false;
          global.reportId = static_cast<uint8_t>(value);
          layout.hasReportIds = true;
          break;
        case 9: global.reportCount = value; break;
        case 10:
          if (itemSize != 0 || globalDepth >= kMaxGlobalDepth) return false;
          globals[globalDepth++] = global;
          break;
        case 11:
          if (itemSize != 0 || globalDepth == 0) return false;
          global = globals[--globalDepth];
          break;
        default: break;
      }
      continue;
    }

    if (type == 2) {  // Local
      const uint16_t itemUsage = itemSize == 4 ? static_cast<uint16_t>(value & 0xFFFFu)
                                                : static_cast<uint16_t>(value & 0xFFFFu);
      switch (tag) {
        case 0:
          if (local.count < 16) local.usages[local.count++] = itemUsage;
          break;
        case 1: local.minimum = itemUsage; local.hasRange = true; break;
        case 2: local.maximum = itemUsage; break;
        default: break;
      }
      continue;
    }

    if (type != 0) continue;
    if (tag == 10) {  // Collection
      if (collectionDepth >= kMaxGlobalDepth) return false;
      collectionStack[collectionDepth] = appGamepadOrJoystick;
      touchCollectionStack[collectionDepth] = appTouch;
      ++collectionDepth;
      if (value == 1 && global.usagePage == 0x01 && local.count > 0 &&
          (local.usages[0] == 0x04 || local.usages[0] == 0x05)) appGamepadOrJoystick = true;
      if (value == 1 && global.usagePage == 0x0D && local.count > 0 &&
          (local.usages[0] == 0x04 || local.usages[0] == 0x05)) appTouch = true;
      local.clear();
      continue;
    }
    if (tag == 12) {  // End Collection
      if (collectionDepth == 0) return false;
      --collectionDepth;
      appGamepadOrJoystick = collectionStack[collectionDepth];
      appTouch = touchCollectionStack[collectionDepth];
      local.clear();
      continue;
    }
    if (tag == 8) {  // Input
      if (global.reportSize == 0 || global.reportCount == 0 || global.reportSize > 32 ||
          global.reportCount > 64) return false;
      Report* report = findReport(layout, global.reportId);
      if (!report) return false;
      const uint32_t bits = global.reportSize * global.reportCount;
      if (bits > 512 || report->inputBits + bits > 512) return false;
      const uint16_t itemBase = report->inputBits;
      report->inputBits = static_cast<uint16_t>(report->inputBits + bits);
      const bool constant = (value & 0x01u) != 0;
      const bool variable = (value & 0x02u) != 0;
      const uint16_t page = global.usagePage <= 0xFFFF ? static_cast<uint16_t>(global.usagePage) : 0;
      if (!constant && (page == 0x07 || page == 0x0C)) report->hasKeyboardOrConsumer = true;

      if (!constant && variable && page != 0x07 && page != 0x0C && (page != 0x0D || appTouch)) {
        for (uint32_t index = 0; index < global.reportCount; ++index) {
          uint16_t usage = 0;
          if (index < local.count) usage = local.usages[index];
          else if (local.hasRange && local.maximum >= local.minimum &&
                   local.minimum + index <= local.maximum) usage = static_cast<uint16_t>(local.minimum + index);
          else if (local.count == 1) usage = local.usages[0];
          FieldKind kind = FieldKind::ButtonBit;
          bool supported = false;
          if (global.reportSize == 1 && page != 0x07 && page != 0x0C && page != 0x0D) {
            kind = FieldKind::ButtonBit;
            supported = true;
          } else if (page == 0x0D && appTouch && usage == 0x42 && global.reportSize == 1) {
            kind = FieldKind::TouchContact;
            supported = true;
          } else if (page == 0x01 && appTouch && usage == 0x30 &&
                     (global.reportSize == 8 || global.reportSize == 16) &&
                     global.logicalMax > global.logicalMin) {
            kind = FieldKind::TouchX;
            supported = true;
          } else if (page == 0x01 && appTouch && usage == 0x31 &&
                     (global.reportSize == 8 || global.reportSize == 16) &&
                     global.logicalMax > global.logicalMin) {
            kind = FieldKind::TouchY;
            supported = true;
          } else if (page == 0x01 && appGamepadOrJoystick && usage == 0x39 &&
                     global.reportSize <= 8 && (value & 0x40u) != 0 && global.logicalMax > global.logicalMin) {
            kind = FieldKind::Hat;
            supported = true;
          } else if (page == 0x01 && appGamepadOrJoystick && usage >= 0x30 && usage <= 0x38 &&
                     (global.reportSize == 8 || global.reportSize == 16) &&
                     global.logicalMax > global.logicalMin) {
            kind = FieldKind::Axis;
            supported = true;
          }
          if (supported && !addField(layout, global.reportId,
                                     static_cast<uint16_t>(itemBase + index * global.reportSize),
                                     static_cast<uint8_t>(global.reportSize), page, usage,
                                     global.logicalMin, global.logicalMax,
                                     static_cast<uint8_t>(value), kind)) return false;
        }
      }
      local.clear();
      continue;
    }
    local.clear();  // Other Main items consume their local state.
  }
  if (globalDepth != 0 || collectionDepth != 0 || layout.overflow || layout.reportCount == 0) return false;

  // Mixed keyboard/consumer report IDs stay on their mature decoder path.
  uint8_t out = 0;
  for (uint8_t i = 0; i < layout.fieldCount; ++i) {
    bool mixed = false;
    for (uint8_t r = 0; r < layout.reportCount; ++r) {
      if (layout.reports[r].id == layout.fields[i].reportId && layout.reports[r].hasKeyboardOrConsumer)
        mixed = true;
    }
    if (!mixed) layout.fields[out++] = layout.fields[i];
  }
  layout.fieldCount = out;
  layout.valid = true;
  return true;
}

inline uint32_t readBits(const uint8_t* payload, const size_t length, const uint16_t bitOffset,
                         const uint8_t bitSize, bool& ok) {
  ok = false;
  if (!payload || bitSize == 0 || bitSize > 16 || bitOffset + bitSize > length * 8) return 0;
  uint32_t result = 0;
  for (uint8_t bit = 0; bit < bitSize; ++bit) {
    const uint16_t at = static_cast<uint16_t>(bitOffset + bit);
    result |= static_cast<uint32_t>((payload[at / 8] >> (at % 8)) & 1u) << bit;
  }
  ok = true;
  return result;
}

inline bool appendEdge(Edge* edges, const uint8_t capacity, uint8_t& count,
                      const uint8_t token, const uint8_t state, const bool pressed) {
  if (!edges || count >= capacity) return false;
  edges[count++] = {token, state, pressed};
  return true;
}

inline int32_t signedValue(const uint32_t raw, const uint8_t bits) {
  if (bits >= 32) return static_cast<int32_t>(raw);
  const uint32_t sign = 1u << (bits - 1);
  return (raw & sign) ? static_cast<int32_t>(raw | (~0u << bits)) : static_cast<int32_t>(raw);
}

// A first report establishes baseline only. Subsequent edges use descriptor
// field position; the caller adds the Report ID and descriptor fingerprint to
// the persisted learned identity. Returns false for truncated/oversized data.
inline bool decode(const Layout& layout, Decoder& decoder, const uint8_t reportId,
                   const uint8_t* payload, const size_t length, Edge* edges,
                   const uint8_t capacity, uint8_t& edgeCount, const uint32_t nowMs = 0) {
  edgeCount = 0;
  if (!layout.valid || !payload || !edges || capacity == 0) return false;
  const Report* report = nullptr;
  for (uint8_t i = 0; i < layout.reportCount; ++i)
    if (layout.reports[i].id == reportId) report = &layout.reports[i];
  if (!report || report->hasKeyboardOrConsumer || (report->inputBits + 7) / 8 != length) return false;

  for (uint8_t i = 0; i < layout.fieldCount; ++i) {
    const Field& field = layout.fields[i];
    if (field.reportId != reportId) continue;
    if (field.kind == FieldKind::TouchContact || field.kind == FieldKind::TouchX ||
        field.kind == FieldKind::TouchY) continue;  // touch fields form a single release-time gesture
    bool ok = false;
    const uint32_t raw = readBits(payload, length, field.bitOffset, field.bitSize, ok);
    if (!ok) { edgeCount = 0; return false; }
    if (field.kind == FieldKind::ButtonBit) {
      const uint8_t state = raw ? 1 : 0;
      if (!decoder.initialized[i]) {
        decoder.buttonState[i] = state;
        decoder.initialized[i] = true;
      } else if (decoder.buttonState[i] != state) {
        decoder.buttonState[i] = state;
        if (!appendEdge(edges, capacity, edgeCount, field.token, 1, state != 0)) return false;
      }
      continue;
    }
    const int32_t value = field.logicalMin < 0 ? signedValue(raw, field.bitSize) : static_cast<int32_t>(raw);
    if (field.kind == FieldKind::Hat) {
      const uint32_t nullValue = static_cast<uint32_t>(field.logicalMax + 1);
      const uint8_t state = raw >= static_cast<uint32_t>(field.logicalMin) && raw <= static_cast<uint32_t>(field.logicalMax)
                                ? static_cast<uint8_t>(raw - field.logicalMin + 1)
                                : (raw == nullValue ? 0 : 0xFF);
      if (state == 0xFF) continue;
      if (!decoder.initialized[i]) {
        decoder.hatState[i] = state;
        decoder.initialized[i] = true;
      } else if (decoder.hatState[i] != state) {
        const uint8_t previous = decoder.hatState[i];
        decoder.hatState[i] = state;
        if (previous && !appendEdge(edges, capacity, edgeCount, field.token, previous, false)) return false;
        if (state && !appendEdge(edges, capacity, edgeCount, field.token, state, true)) return false;
      }
      continue;
    }

    const int64_t span = static_cast<int64_t>(field.logicalMax) - field.logicalMin;
    if (span <= 0) continue;
    const int64_t center = static_cast<int64_t>(field.logicalMin) + span / 2;
    const int64_t displacement = static_cast<int64_t>(value) - center;
    const int64_t threshold = span * 28 / 100;
    const int64_t releaseBand = span * 14 / 100;
    int8_t next = decoder.axisState[i];
    if (displacement <= -threshold) next = -1;
    else if (displacement >= threshold) next = 1;
    else if ((next < 0 && displacement > -releaseBand) || (next > 0 && displacement < releaseBand)) next = 0;
    if (!decoder.initialized[i]) {
      decoder.axisState[i] = next;
      decoder.initialized[i] = true;
    } else if (decoder.axisState[i] != next) {
      const int8_t previous = decoder.axisState[i];
      decoder.axisState[i] = next;
      if (previous && !appendEdge(edges, capacity, edgeCount, field.token,
                                  previous < 0 ? 1 : 2, false)) return false;
      if (next && !appendEdge(edges, capacity, edgeCount, field.token,
                              next < 0 ? 1 : 2, true)) return false;
    }
  }

  // Generic touch learning is enabled only for descriptors which declare a
  // Touch Screen/Touch Pad application plus Tip Switch and X/Y in one numbered
  // report. Raw coordinates never become identities; one direction/tap token
  // is emitted only after a bounded contact cycle completes.
  uint8_t reportIndex = 0xFF, contactIndex = 0xFF, xIndex = 0xFF, yIndex = 0xFF;
  uint8_t contactCount = 0;
  for (uint8_t r = 0; r < layout.reportCount; ++r)
    if (layout.reports[r].id == reportId) reportIndex = r;
  for (uint8_t i = 0; i < layout.fieldCount; ++i) {
    const Field& field = layout.fields[i];
    if (field.reportId != reportId) continue;
    if (field.kind == FieldKind::TouchContact) {
      if (contactIndex == 0xFF) contactIndex = i;
      ++contactCount;
    } else if (field.kind == FieldKind::TouchX) {
      if (xIndex == 0xFF) xIndex = i;
    } else if (field.kind == FieldKind::TouchY) {
      if (yIndex == 0xFF) yIndex = i;
    }
  }
  // Multitouch descriptors may declare further contact slots. Learn only the
  // first descriptor-ordered contact and its first X/Y pair, so one contact
  // cycle still produces at most one action and coordinates are never IDs.
  if (reportIndex < kMaxReports && contactCount > 0 && xIndex != 0xFF && yIndex != 0xFF) {
    const Field& contactField = layout.fields[contactIndex];
    const Field& xField = layout.fields[xIndex];
    const Field& yField = layout.fields[yIndex];
    bool contactOk = false, xOk = false, yOk = false;
    const uint32_t contactRaw = readBits(payload, length, contactField.bitOffset, contactField.bitSize, contactOk);
    const uint32_t xRaw = readBits(payload, length, xField.bitOffset, xField.bitSize, xOk);
    const uint32_t yRaw = readBits(payload, length, yField.bitOffset, yField.bitSize, yOk);
    const int32_t x = xField.logicalMin < 0 ? signedValue(xRaw, xField.bitSize) : static_cast<int32_t>(xRaw);
    const int32_t y = yField.logicalMin < 0 ? signedValue(yRaw, yField.bitSize) : static_cast<int32_t>(yRaw);
    const int64_t spanX = static_cast<int64_t>(xField.logicalMax) - xField.logicalMin;
    const int64_t spanY = static_cast<int64_t>(yField.logicalMax) - yField.logicalMin;
    if (!contactOk || !xOk || !yOk || spanX <= 0 || spanY <= 0 ||
        x < xField.logicalMin || x > xField.logicalMax || y < yField.logicalMin || y > yField.logicalMax) {
      decoder.touch[reportIndex] = Decoder::TouchState{};
      return false;
    }
    auto& touch = decoder.touch[reportIndex];
    const bool down = contactRaw != 0;
    if (!touch.initialized) {
      touch.initialized = true;
      touch.contact = down;
      if (down) {
        touch.startX = touch.lastX = x;
        touch.startY = touch.lastY = y;
        touch.startedMs = touch.lastMs = nowMs;
      }
    } else if (down) {
      const bool expired = touch.contact && nowMs != 0 &&
          (static_cast<uint32_t>(nowMs - touch.startedMs) > kTouchMaxDurationMs ||
           static_cast<uint32_t>(nowMs - touch.lastMs) > kTouchMaxInterReportMs);
      if (!touch.contact || expired) {
        touch.startX = touch.lastX = x;
        touch.startY = touch.lastY = y;
        touch.startedMs = nowMs;
      } else {
        touch.lastX = x;
        touch.lastY = y;
      }
      touch.lastMs = nowMs;
      touch.contact = true;
    } else if (touch.contact) {
      const bool timely = nowMs == 0 ||
          (static_cast<uint32_t>(nowMs - touch.startedMs) <= kTouchMaxDurationMs &&
           static_cast<uint32_t>(nowMs - touch.lastMs) <= kTouchMaxInterReportMs);
      if (timely) {
        const int64_t dx = static_cast<int64_t>(touch.lastX) - touch.startX;
        const int64_t dy = static_cast<int64_t>(touch.lastY) - touch.startY;
        const int64_t ax = dx < 0 ? -dx : dx;
        const int64_t ay = dy < 0 ? -dy : dy;
        // Calibrated against the X4 captures: descriptor logical ranges can be
        // much wider than the controller's emitted coordinate range. A 1/64
        // span displacement still rejects small jitter while recognizing the
        // captured ~1.1k-unit vertical swipes on a signed 16-bit descriptor.
        const int64_t thresholdX = spanX / 64 > 0 ? spanX / 64 : 1;
        const int64_t thresholdY = spanY / 64 > 0 ? spanY / 64 : 1;
        uint8_t gesture = 0;
        if (ax <= thresholdX / 2 && ay <= thresholdY / 2) gesture = 1;  // tap
        else if (ax >= thresholdX && ax * 2 >= ay * 3) gesture = dx < 0 ? 2 : 3;
        else if (ay >= thresholdY && ay * 2 >= ax * 3) gesture = dy < 0 ? 4 : 5;
        if (gesture) {
          if (edgeCount > capacity || capacity - edgeCount < 2) return false;
          edges[edgeCount++] = {contactField.token, gesture, true};
          edges[edgeCount++] = {contactField.token, gesture, false};
        }
      }
      touch.contact = false;
      touch.lastMs = nowMs;
    } else {
      touch.lastMs = nowMs;
    }
  }
  return true;
}

// Last-resort fallback when a valid Input Report Reference exists but its
// Report Map cannot be read/parsed. Only one-bit transitions in a bounded,
// fixed-length report are learnable. Multi-bit deltas, packet-length changes,
// stale baselines and overlapping controls fail closed. Identity is scoped by
// the caller to device address + report metadata.
inline bool decodeRawBitFallbackCycle(RawBitFallback& fallback, const uint32_t identity,
                                      const uint8_t* payload, const size_t length, const uint32_t nowMs,
                                      Edge& press, Edge& release) {
  if (!payload || length == 0 || length > kRawFallbackMaxBytes || identity == 0) return false;
  if (!fallback.initialized || fallback.identity != identity || fallback.length != length ||
      static_cast<uint32_t>(nowMs - fallback.lastMs) > kRawFallbackMaxGapMs) {
    fallback.reset();
    for (size_t i = 0; i < length; ++i) fallback.previous[i] = payload[i];
    fallback.identity = identity;
    fallback.length = static_cast<uint8_t>(length);
    fallback.lastMs = nowMs;
    fallback.initialized = true;
    return false;
  }

  uint8_t changedCount = 0;
  uint8_t changedBit = 0xFF;
  bool nextPressed = false;
  for (size_t byte = 0; byte < length; ++byte) {
    const uint8_t delta = static_cast<uint8_t>(fallback.previous[byte] ^ payload[byte]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((delta & (1u << bit)) == 0) continue;
      ++changedCount;
      changedBit = static_cast<uint8_t>(byte * 8 + bit);
      nextPressed = (payload[byte] & (1u << bit)) != 0;
    }
  }
  for (size_t i = 0; i < length; ++i) fallback.previous[i] = payload[i];
  fallback.lastMs = nowMs;
  if (changedCount != 1) {
    fallback.activeBit = 0xFF;
    return false;
  }

  if (fallback.activeBit == 0xFF) {
    if (!nextPressed) return false;
    fallback.activeBit = changedBit;
    return false;  // baseline -> press is held pending a matching release
  }
  if (fallback.activeBit == changedBit && !nextPressed) {
    fallback.activeBit = 0xFF;
    press = {changedBit, 1, true};
    release = {changedBit, 1, false};
    return true;
  }
  fallback.activeBit = 0xFF;
  return false;
}

}  // namespace bleinput::hid::learning
