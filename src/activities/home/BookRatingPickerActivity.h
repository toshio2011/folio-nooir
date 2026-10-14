#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

class BookRatingPickerActivity final : public Activity {
 public:
  BookRatingPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                           uint8_t currentRating);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath_;
  uint8_t originalRating_ = 0;
  uint8_t selectedRating_ = 0;
  bool ignoreInitialConfirm = false;

  void adjust(int direction);
  void saveAndClose();
  void cancelAndClose();
};
