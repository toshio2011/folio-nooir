#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

class BookDatePickerActivity final : public Activity {
 public:
  BookDatePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                         uint32_t startDate, uint32_t finishDate, uint32_t lastReadDate, uint32_t todayDate);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookPath_;
  uint32_t originalDates_[3] = {};
  uint32_t selectedDates_[3] = {};
  uint32_t todayDate_ = 0;
  uint8_t selectedField_ = 0;

  void selectField(int direction);
  void adjustDate(int direction);
  void saveAndClose();
  void cancelAndClose();
};
