#pragma once

#include <cstdint>

namespace keyboard_navigation {

template <typename Row>
int keyCenterTwice(const Row& row, const int column, const int rectWidth, const int gap) {
  if (row.keys == nullptr || row.count == 0 || column < 0 || column >= row.count) return 0;
  int units = static_cast<int>(row.insetUnits) * 2;
  for (int index = 0; index < row.count; ++index) {
    const int width = row.keys[index].widthUnits ? row.keys[index].widthUnits : 1;
    units += width;
  }
  if (units <= 0) return 0;

  const int unitWidth = (rectWidth - gap * (static_cast<int>(row.count) - 1)) / units;
  int x = static_cast<int>(row.insetUnits) * unitWidth;
  const int rowRight = rectWidth - static_cast<int>(row.insetUnits) * unitWidth;
  for (int index = 0; index < row.count; ++index) {
    const int keyUnits = row.keys[index].widthUnits ? row.keys[index].widthUnits : 1;
    const int width = index == row.count - 1 ? rowRight - x : unitWidth * keyUnits;
    if (index == column) return x * 2 + width;
    x += width + gap;
  }
  return 0;
}

template <typename Row>
int nearestColumnByVisualCenter(const Row& sourceRow, const int sourceColumn, const Row& targetRow,
                                const int rectWidth, const int gap) {
  if (targetRow.keys == nullptr || targetRow.count == 0) return 0;
  const int sourceCenter = keyCenterTwice(sourceRow, sourceColumn, rectWidth, gap);
  int nearestColumn = 0;
  int nearestDistance = -1;
  for (int column = 0; column < targetRow.count; ++column) {
    const int center = keyCenterTwice(targetRow, column, rectWidth, gap);
    const int distance = center >= sourceCenter ? center - sourceCenter : sourceCenter - center;
    // Keeping the earlier key on exact ties makes row transitions stable.
    if (nearestDistance < 0 || distance < nearestDistance) {
      nearestDistance = distance;
      nearestColumn = column;
    }
  }
  return nearestColumn;
}

constexpr int moveRow(const int selectedRow, const int rowCount, const int delta) {
  if (rowCount <= 0) return 0;
  const int current = selectedRow < 0 ? 0 : (selectedRow >= rowCount ? rowCount - 1 : selectedRow);
  if (delta < 0) return current > 0 ? current - 1 : current;
  if (delta > 0) return current + 1 < rowCount ? current + 1 : current;
  return current;
}

struct KeyRepeatGate {
  bool held = false;
  bool hasRepeated = false;
  bool suppressUntilRelease = false;
  uint32_t pressedAt = 0;
  uint32_t lastRepeatAt = 0;

  void reset(const bool suppressHeldInput = false) {
    held = false;
    hasRepeated = false;
    suppressUntilRelease = suppressHeldInput;
    pressedAt = 0;
    lastRepeatAt = 0;
  }

  bool update(const bool wasPressed, const bool isPressed, const bool wasReleased, const uint32_t now,
              const uint32_t repeatStartMs = 500, const uint32_t repeatIntervalMs = 500) {
    if (suppressUntilRelease) {
      if (wasReleased || !isPressed) reset();
      return false;
    }

    // A short click can deliver both edges in one input poll. Preserve the
    // press action once, while leaving the gate released for the next click.
    if (wasPressed) {
      if (isPressed && !wasReleased) {
        held = true;
        hasRepeated = false;
        pressedAt = now;
        lastRepeatAt = 0;
      } else {
        reset();
      }
      return true;
    }

    if (wasReleased || !isPressed) {
      reset();
      return false;
    }

    // If the key was already held when this gate became active, wait for a
    // release instead of converting that stale hold into a navigation step.
    if (!held) {
      held = true;
      hasRepeated = false;
      pressedAt = now;
      return false;
    }
    if (now - pressedAt < repeatStartMs) return false;
    if (hasRepeated && now - lastRepeatAt < repeatIntervalMs) return false;
    hasRepeated = true;
    lastRepeatAt = now;
    return true;
  }
};

// Horizontal navigation stays on the current keyboard row. In particular,
// holding Right on the bottom row must not wrap from OK back to its left edge.
constexpr int moveColumn(const int selectedColumn, const int columnCount, const int delta) {
  if (columnCount <= 0) return 0;
  const int current = selectedColumn < 0 ? 0 : (selectedColumn >= columnCount ? columnCount - 1 : selectedColumn);
  if (delta < 0) return current > 0 ? current - 1 : current;
  if (delta > 0) return current + 1 < columnCount ? current + 1 : current;
  return current;
}

}  // namespace keyboard_navigation
