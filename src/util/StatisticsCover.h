#pragma once

#include <string>

class GfxRenderer;
struct Rect;
struct StatisticsBookSnapshot;

constexpr int STATISTICS_COVER_HEIGHT = 220;

// Resolves and validates the existing 220px thumbnail once for this snapshot.
// It never generates a thumbnail or parses the source book.
bool ensureStatisticsCover(StatisticsBookSnapshot& book);

// Selects an already-existing 360px Carousel thumbnail when available and
// otherwise returns the validated 220px thumbnail. This never generates or
// prepares a cover.
std::string selectStatisticsCoverPath(const StatisticsBookSnapshot& book);

// Draws one already-validated cached thumbnail, fitted inside bounds.
// Returns false if the cached file can no longer be opened or parsed.
bool drawStatisticsCover(GfxRenderer& renderer, const std::string& path, const Rect& bounds);
