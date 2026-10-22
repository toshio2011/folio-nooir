#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Persisted sleep-mode values. Keep these assignments aligned with
// CrossPointSettings::SLEEP_SCREEN_MODE; Flexible is appended after the 1.6.6
// modes so settings written by earlier firmware keep their meaning.
namespace SleepScreenModeValue {
enum : uint8_t {
  DARK = 0,
  LIGHT = 1,
  CUSTOM = 2,
  COVER = 3,
  COVER_CUSTOM = 4,
  BLANK = 5,
  QUICK_RESUME = 6,
  OVERLAY = 7,
  COVER_OVERLAY = 8,
  READING_STATS_SLEEP = 9,
  MINIMAL_STATS = 10,
  CLIPPING_COVER = 11,
  TODO_LIST = 12,
  READING_CALENDAR_SLEEP = 13,
  READING_SUMMARY_SLEEP = 14,
  COVER_PROGRESS = 15,
  RANDOM = 16,
  FLEXIBLE_COMPOSITE = 17,
};
}  // namespace SleepScreenModeValue

enum class SleepComposerElement : uint8_t {
  None = 0,
  Dark = 1,
  Light = 2,
  Blank = 3,
  NooirDefault = 4,
  CustomImage = 5,
  CurrentBookCover = 6,
  CurrentPage = 7,
  TransparentPng = 8,
  BookTitleAuthor = 9,
  BookProgress = 10,
  BookStats = 11,
  ReadingCalendar = 12,
  ReadingSummary = 13,
  ToDo = 14,
  ClippingQuote = 15,
  ClockDate = 16,
  Random = 17,
  Count = 18,
};

enum class SleepComposerRole : uint8_t { Base, OverlayPrimary, OverlaySecondary };

struct SleepComposerSettingsValues {
  uint8_t background = static_cast<uint8_t>(SleepComposerElement::CurrentBookCover);
  uint8_t overlayA = static_cast<uint8_t>(SleepComposerElement::ReadingCalendar);
  uint8_t overlayB = static_cast<uint8_t>(SleepComposerElement::ClockDate);
};

inline SleepComposerSettingsValues normalizeSleepComposerSettings(const uint8_t background, const uint8_t overlayA,
                                                                  const uint8_t overlayB) {
  SleepComposerSettingsValues result;
  result.background = background < static_cast<uint8_t>(SleepComposerElement::Count)
                          ? background
                          : static_cast<uint8_t>(SleepComposerElement::CurrentBookCover);
  result.overlayA = overlayA < static_cast<uint8_t>(SleepComposerElement::Count)
                        ? overlayA
                        : static_cast<uint8_t>(SleepComposerElement::ReadingCalendar);
  result.overlayB = overlayB < static_cast<uint8_t>(SleepComposerElement::Count)
                        ? overlayB
                        : static_cast<uint8_t>(SleepComposerElement::ClockDate);
  return result;
}

inline bool sleepComposerHasRandomElement(const uint8_t background, const uint8_t overlayA, const uint8_t overlayB) {
  const auto settings = normalizeSleepComposerSettings(background, overlayA, overlayB);
  constexpr auto randomElement = static_cast<uint8_t>(SleepComposerElement::Random);
  return settings.background == randomElement || settings.overlayA == randomElement ||
         settings.overlayB == randomElement;
}

struct SleepComposerAvailability {
  bool customImage = false;
  bool bookCover = false;
  bool currentBook = false;
  bool currentPage = false;
  bool transparentPng = false;
  bool readingCalendar = false;
  bool readingSummary = true;
  bool todo = false;
  bool clipping = false;
  bool clockDate = false;
};

constexpr size_t SLEEP_COMPOSER_MAX_CANDIDATES = 17;

struct SleepComposerCandidates {
  std::array<SleepComposerElement, SLEEP_COMPOSER_MAX_CANDIDATES> elements{};
  size_t count = 0;
};

inline bool sleepComposerElementAvailable(const SleepComposerElement element, const SleepComposerRole role,
                                          const SleepComposerAvailability& available) {
  if (element == SleepComposerElement::None) return role != SleepComposerRole::Base;
  switch (element) {
    case SleepComposerElement::CustomImage: return available.customImage;
    case SleepComposerElement::CurrentBookCover: return available.bookCover;
    case SleepComposerElement::CurrentPage: return available.currentPage;
    case SleepComposerElement::TransparentPng: return available.transparentPng;
    case SleepComposerElement::BookTitleAuthor:
    case SleepComposerElement::BookProgress:
    case SleepComposerElement::BookStats: return available.currentBook;
    case SleepComposerElement::ReadingCalendar: return available.readingCalendar;
    case SleepComposerElement::ReadingSummary: return available.readingSummary;
    case SleepComposerElement::ToDo: return available.todo;
    case SleepComposerElement::ClippingQuote: return available.clipping;
    case SleepComposerElement::ClockDate: return available.clockDate;
    case SleepComposerElement::Random:
    case SleepComposerElement::Count: return false;
    case SleepComposerElement::Dark:
    case SleepComposerElement::Light:
    case SleepComposerElement::Blank:
    case SleepComposerElement::NooirDefault: return true;
    case SleepComposerElement::None: return role != SleepComposerRole::Base;
  }
  return false;
}

inline SleepComposerCandidates buildSleepComposerCandidates(const SleepComposerRole role,
                                                              const SleepComposerAvailability& available) {
  static constexpr std::array<SleepComposerElement, SLEEP_COMPOSER_MAX_CANDIDATES> choices = {
      SleepComposerElement::None,
      SleepComposerElement::Dark,
      SleepComposerElement::Light,
      SleepComposerElement::Blank,
      SleepComposerElement::NooirDefault,
      SleepComposerElement::CustomImage,
      SleepComposerElement::CurrentBookCover,
      SleepComposerElement::CurrentPage,
      SleepComposerElement::TransparentPng,
      SleepComposerElement::BookTitleAuthor,
      SleepComposerElement::BookProgress,
      SleepComposerElement::BookStats,
      SleepComposerElement::ReadingCalendar,
      SleepComposerElement::ReadingSummary,
      SleepComposerElement::ToDo,
      SleepComposerElement::ClippingQuote,
      SleepComposerElement::ClockDate,
  };
  SleepComposerCandidates result;
  for (const auto element : choices) {
    if (sleepComposerElementAvailable(element, role, available)) result.elements[result.count++] = element;
  }
  return result;
}

inline bool sleepComposerContains(const SleepComposerElement* values, const size_t count,
                                  const SleepComposerElement value) {
  for (size_t i = 0; i < count; ++i) {
    if (values[i] == value) return true;
  }
  return false;
}

inline SleepComposerElement resolveSleepComposerElement(const SleepComposerElement configured,
                                                         const SleepComposerRole role,
                                                         const SleepComposerAvailability& available,
                                                         const uint32_t randomValue,
                                                         const SleepComposerElement* earlierLayers = nullptr,
                                                         const size_t earlierLayerCount = 0) {
  if (configured != SleepComposerElement::Random) {
    if (sleepComposerElementAvailable(configured, role, available)) return configured;
    return role == SleepComposerRole::Base ? SleepComposerElement::NooirDefault : SleepComposerElement::None;
  }

  const auto candidates = buildSleepComposerCandidates(role, available);
  std::array<SleepComposerElement, SLEEP_COMPOSER_MAX_CANDIDATES> distinct{};
  size_t distinctCount = 0;
  for (size_t i = 0; i < candidates.count; ++i) {
    const auto candidate = candidates.elements[i];
    if (!sleepComposerContains(earlierLayers, earlierLayerCount, candidate)) distinct[distinctCount++] = candidate;
  }
  const auto* choices = distinctCount > 0 ? distinct.data() : candidates.elements.data();
  const size_t choiceCount = distinctCount > 0 ? distinctCount : candidates.count;
  if (choiceCount == 0) return role == SleepComposerRole::Base ? SleepComposerElement::NooirDefault
                                                               : SleepComposerElement::None;
  return choices[randomValue % choiceCount];
}

inline bool sleepComposerNeedsStatisticsSnapshot(const SleepComposerElement* elements, const size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (elements[i] == SleepComposerElement::ReadingCalendar ||
        elements[i] == SleepComposerElement::ReadingSummary) {
      return true;
    }
  }
  return false;
}

struct SleepComposerRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Aspect-fit an image into a sleep-screen region. 64-bit products keep corrupt
// or unusually large BMP dimensions from overflowing, and rounding the scaled
// dimension prevents very thin images from truncating to a zero-sized draw.
inline SleepComposerRect sleepComposerFitRect(const int sourceWidth, const int sourceHeight,
                                               const SleepComposerRect& bounds) {
  if (sourceWidth <= 0 || sourceHeight <= 0 || bounds.width <= 0 || bounds.height <= 0) return {};

  const int64_t widthAtBoundHeight = static_cast<int64_t>(sourceWidth) * bounds.height;
  const int64_t heightAtBoundWidth = static_cast<int64_t>(sourceHeight) * bounds.width;
  int drawWidth = bounds.width;
  int drawHeight = bounds.height;
  if (widthAtBoundHeight > heightAtBoundWidth) {
    drawHeight = static_cast<int>((static_cast<int64_t>(sourceHeight) * bounds.width + sourceWidth / 2) /
                                  sourceWidth);
    drawHeight = std::max(1, std::min(bounds.height, drawHeight));
  } else {
    drawWidth = static_cast<int>((static_cast<int64_t>(sourceWidth) * bounds.height + sourceHeight / 2) /
                                 sourceHeight);
    drawWidth = std::max(1, std::min(bounds.width, drawWidth));
  }
  return {bounds.x + (bounds.width - drawWidth) / 2, bounds.y + (bounds.height - drawHeight) / 2,
          drawWidth, drawHeight};
}

enum class SleepComposerSlot : uint8_t {
  None,
  Top,
  Center,
  Bottom,
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
  CenterTop,
  CenterBottom,
};

inline bool sleepComposerRectsOverlap(const SleepComposerRect& a, const SleepComposerRect& b) {
  return a.width > 0 && a.height > 0 && b.width > 0 && b.height > 0 && a.x < b.x + b.width &&
         b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

inline SleepComposerRect sleepComposerSlotRect(const SleepComposerRole role, const SleepComposerSlot slot,
                                                const int screenWidth, const int screenHeight) {
  const int margin = 14;
  const bool primary = role == SleepComposerRole::OverlayPrimary;
  const int width = primary ? std::max(1, screenWidth * 82 / 100) : std::max(1, screenWidth * 46 / 100);
  const int height = primary ? std::max(96, std::min(156, screenHeight / 4)) : std::max(64, std::min(88, screenHeight / 10));
  int x = (screenWidth - width) / 2;
  int y = margin;
  switch (slot) {
    case SleepComposerSlot::Top: y = margin; break;
    case SleepComposerSlot::Center: y = (screenHeight - height) / 2; break;
    case SleepComposerSlot::Bottom: y = screenHeight - height - margin; break;
    case SleepComposerSlot::TopLeft: x = margin; y = margin; break;
    case SleepComposerSlot::TopRight: x = screenWidth - width - margin; y = margin; break;
    case SleepComposerSlot::BottomLeft: x = margin; y = screenHeight - height - margin; break;
    case SleepComposerSlot::BottomRight: x = screenWidth - width - margin; y = screenHeight - height - margin; break;
    case SleepComposerSlot::CenterTop: x = (screenWidth - width) / 2; y = screenHeight / 3 - height / 2; break;
    case SleepComposerSlot::CenterBottom:
      x = (screenWidth - width) / 2;
      y = screenHeight * 2 / 3 - height / 2;
      break;
    case SleepComposerSlot::None: return {};
  }
  return {x, y, width, height};
}

inline SleepComposerSlot chooseSleepComposerOverlaySlot(const SleepComposerRole role, const int screenWidth,
                                                         const int screenHeight,
                                                         const SleepComposerRect* occupied = nullptr,
                                                         const size_t occupiedCount = 0) {
  static constexpr std::array<SleepComposerSlot, 3> primarySlots = {
      SleepComposerSlot::Top, SleepComposerSlot::Center, SleepComposerSlot::Bottom};
  static constexpr std::array<SleepComposerSlot, 6> secondarySlots = {
      SleepComposerSlot::TopRight, SleepComposerSlot::BottomRight, SleepComposerSlot::TopLeft,
      SleepComposerSlot::BottomLeft, SleepComposerSlot::CenterTop, SleepComposerSlot::CenterBottom};
  if (role == SleepComposerRole::OverlayPrimary) {
    for (const auto slot : primarySlots) {
      const auto rect = sleepComposerSlotRect(role, slot, screenWidth, screenHeight);
      bool collision = false;
      for (size_t i = 0; i < occupiedCount; ++i) collision |= sleepComposerRectsOverlap(rect, occupied[i]);
      if (!collision) return slot;
    }
  } else if (role == SleepComposerRole::OverlaySecondary) {
    for (const auto slot : secondarySlots) {
      const auto rect = sleepComposerSlotRect(role, slot, screenWidth, screenHeight);
      bool collision = false;
      for (size_t i = 0; i < occupiedCount; ++i) collision |= sleepComposerRectsOverlap(rect, occupied[i]);
      if (!collision) return slot;
    }
  }
  return SleepComposerSlot::None;
}

inline bool sleepComposerRectFitsSlot(const SleepComposerRect* occupied, const size_t occupiedCount,
                                      const SleepComposerRect& candidate) {
  for (size_t i = 0; i < occupiedCount; ++i) {
    if (sleepComposerRectsOverlap(candidate, occupied[i])) return false;
  }
  return true;
}
