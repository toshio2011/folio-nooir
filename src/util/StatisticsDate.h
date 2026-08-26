#pragma once

#include <cstdint>

namespace StatisticsDate {

bool split(uint32_t dateKey, int& year, int& month, int& day);
bool isValid(uint32_t dateKey);
int daysInMonth(int year, int month);
int64_t ordinal(uint32_t dateKey);
int weekdayMondayFirst(uint32_t dateKey);

}  // namespace StatisticsDate
