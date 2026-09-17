#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"
#include "util/Dictionary.h"
#include "util/DictionaryRegistry.h"
#include "util/DictionaryResultModel.h"

// Paged plain-text viewer for one dictionary definition. The definition is
// word-wrapped once on entry; each page renders spans of the original string,
// so no per-line copies are held.
class DictionaryDefinitionActivity final : public Activity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, std::string dictionaryName, std::string lookupWord,
                                        DictionarySourceMatches matchingSources,
                                        DictionaryOpenFailureLedger dictionaryOpenFailures,
                                        std::string preferredDictionaryName)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        headword(std::move(headword)),
        definition(std::move(definition)),
        dictionaryName(std::move(dictionaryName)),
        lookupWord(std::move(lookupWord)),
        matchingSources(std::move(matchingSources)),
        dictionaryOpenFailures(std::move(dictionaryOpenFailures)),
        preferredDictionaryName(std::move(preferredDictionaryName)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One wrapped display line: a byte span of `definition`. Wrapping keeps
  // lines under the screen width, so uint16_t length is ample.
  struct Line {
    uint32_t start;
    uint16_t len;
  };

  void wrapText();
  int measureSpan(int fontId, const char* text, size_t len) const;
  void drawBody(int fontId, int x, int startY) const;
  void ensureDictionaryEntries();
  bool discoverNextMatchingSource();
  void refreshDictionaryOptions();
  void openDictionaryMenu();
  void navigateSource(int direction);
  void navigateDefinitionPage(int direction);
  void loadFromDictionary(const std::string& name);
  bool openDictionary(Dictionary& target, const std::string& name);
  void showDictionaryMessage(StrId message);
  size_t currentSourceIndex() const;
  bool currentSourceIsPreferred() const;

  std::string headword;
  std::string dictionaryName;
  const std::string lookupWord;
  // Not const: onEnter() normalizes embedded NULs (StarDict multi-type
  // separators) to newlines so C-string APIs see the whole text.
  std::string definition;
  std::vector<Line> lines;
  int currentPage = 0;
  int totalPages = 1;
  int linesPerPage = 1;
  ButtonNavigator buttonNavigator;
  OptionPopup dictionaryPopup;
  std::vector<std::string> dictionaryOptions;
  bool dictionaryOptionsReady = false;
  std::vector<DictionaryEntry> dictionaryEntries;
  size_t dictionaryEntryCursor = 0;
  bool dictionaryEntriesReady = false;
  bool dictionaryBusy = false;
  bool suppressBackRelease = false;
  bool showDictionaryError = false;
  unsigned long dictionaryMessageTime = 0;
  StrId dictionaryMessage = StrId::STR_DICT_NOT_FOUND;
  DictionarySourceMatches matchingSources;
  DictionaryOpenFailureLedger dictionaryOpenFailures;
  const std::string preferredDictionaryName;
};
