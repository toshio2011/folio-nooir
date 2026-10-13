#pragma once

#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class VisualPreviewActivity final : public Activity {
  enum class Tab : uint8_t { Featured = 0, Cover = 1, Spine = 2 };

  Tab tab_ = Tab::Featured;
  uint8_t selectedRow_ = 0;
  uint8_t spineTarget_ = 0;  // 0 = Recent, 1 = Finished; preview-only choice.
  uint32_t rerollSeed_ = 0x5A17C3u;
  std::vector<TabInfo> tabs_;
  std::vector<Tab> tabKinds_;
  OptionPopup optionPopup_;

  int rowCount() const;
  int previewPanelHeight(int availableHeight) const;
  void selectTab(int index);
  void activateRow();
  void showEnumPicker(StrId title, const StrId* values, size_t count, uint8_t current,
                      uint8_t CrossPointSettings::* setting);
  void renderFeatured(int x, int y, int width, int height);
  void renderCover(int x, int y, int width, int height);
  void renderSpine(int x, int y, int width, int height);
  void renderControls(int x, int y, int width, int height);
  bool handleTouch(int listTop, int listHeight, int controlColumns, int rowHeight);

 public:
  explicit VisualPreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
