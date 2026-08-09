#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/Dictionary.h"
#include "util/DictionaryRegistry.h"

// Explicit, one-dictionary-at-a-time StarDict sidecar preparation. Indexing is
// deliberately kept out of the reader's fallback path so a missing word can
// never trigger several long SD-card scans.
class DictionaryIndexActivity final : public Activity {
 public:
  explicit DictionaryIndexActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DictionaryIndexes", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { List, Indexing, Complete, Cancelled, Failed };

  void refreshEntries();
  void beginIndex();
  void finishFromResult();
  static void indexYield(void* ctx);
  static bool indexProgress(void* ctx, uint32_t processedBytes, uint32_t totalBytes);

  std::vector<DictionaryEntry> entries;
  std::vector<bool> indexed;
  std::vector<bool> resumable;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  State state = State::List;
  std::string statusName;
  uint8_t progressPercent = 0;
  unsigned long lastProgressRenderMs = 0;
  bool resuming = false;
  bool suppressBackRelease = false;
};
