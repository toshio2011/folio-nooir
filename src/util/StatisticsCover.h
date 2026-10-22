#pragma once

#include <string>

class GfxRenderer;
struct Rect;
struct StatisticsBookSnapshot;

constexpr int STATISTICS_COVER_HEIGHT = 220;

// Resolves one complete cached XTC cover or the existing 220px thumbnail once
// for this snapshot. It never generates a cover or parses the source book.
bool ensureStatisticsCover(StatisticsBookSnapshot& book);

// Selects an already-existing full-resolution XTC cover first, then a 360px
// Carousel thumbnail, and otherwise returns the validated 220px thumbnail.
// This never generates or prepares a cover.
std::string selectStatisticsCoverPath(const StatisticsBookSnapshot& book);

// Draws one bounded cached cover with its aspect ratio preserved, fitting and
// upscaling it inside bounds. Returns false if the cached file cannot be read.
bool drawStatisticsCover(GfxRenderer& renderer, const std::string& path, const Rect& bounds);
