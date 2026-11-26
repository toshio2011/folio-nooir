#pragma once
class UITheme {
 public:
  struct Metrics { int topPadding=0, headerHeight=40, verticalSpacing=8, buttonHintsHeight=32; };
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const Metrics& getMetrics() const { return metrics; }
 private:
  Metrics metrics;
};
