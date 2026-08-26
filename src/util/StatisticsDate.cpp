#include "StatisticsDate.h"

namespace StatisticsDate {

namespace {
bool leapYear(const int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
}  // namespace

int daysInMonth(const int year, const int month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1 || month < 1 || month > 12) return 0;
  if (month == 2 && leapYear(year)) return 29;
  return DAYS[month - 1];
}

bool split(const uint32_t dateKey, int& year, int& month, int& day) {
  if (dateKey == 0) return false;
  year = static_cast<int>(dateKey / 10000);
  month = static_cast<int>((dateKey / 100) % 100);
  day = static_cast<int>(dateKey % 100);
  return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month);
}

bool isValid(const uint32_t dateKey) {
  int year = 0;
  int month = 0;
  int day = 0;
  return split(dateKey, year, month, day);
}

int64_t ordinal(const uint32_t dateKey) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!split(dateKey, year, month, day)) return -1;
  const int adjustedYear = year - (month <= 2 ? 1 : 0);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned monthPrime = static_cast<unsigned>(month > 2 ? month - 3 : month + 9);
  const unsigned dayOfYear = (153 * monthPrime + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra);
}

int weekdayMondayFirst(const uint32_t dateKey) {
  const int64_t day = ordinal(dateKey);
  if (day < 0) return -1;
  // The ordinal used above is 719468 on 1970-01-01, which was a Thursday.
  int weekday = static_cast<int>((day - 5) % 7);
  if (weekday < 0) weekday += 7;
  return weekday;
}

}  // namespace StatisticsDate
