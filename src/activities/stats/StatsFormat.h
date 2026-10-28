#pragma once

#include <I18n.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "ReadingStatsStore.h"

// Shared formatting for the two statistics screens. Both render the same kinds
// of figure, and a duration that reads "1h 5m" on one screen and "65m" on the
// other is the sort of thing that makes a report look untrustworthy.
namespace statsformat {

// Coarse by design: minutes are what a reader checks, and a seconds column
// next to an hours column invites comparing numbers of different orders.
inline std::string duration(const uint32_t seconds) {
  char buf[24];
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (hours > 0) {
    std::snprintf(buf, sizeof(buf), "%uh %um", static_cast<unsigned>(hours), static_cast<unsigned>(minutes));
  } else if (minutes > 0) {
    std::snprintf(buf, sizeof(buf), "%um", static_cast<unsigned>(minutes));
  } else {
    std::snprintf(buf, sizeof(buf), "%us", static_cast<unsigned>(seconds));
  }
  return buf;
}

// Day numbers are stored, not timestamps, so a date is all that can be shown --
// and all that matters for "when did I start this".
inline std::string date(const uint16_t day) {
  if (day == 0) return tr(STR_STATS_UNKNOWN_DATE);
  int year = 0;
  int month = 0;
  int dayOfMonth = 0;
  ReadingStatsStore::civilFromDay(day, year, month, dayOfMonth);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d", dayOfMonth, month, year);
  return buf;
}

inline std::string percent(const uint16_t basisPoints) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>((basisPoints + 50) / 100));
  return buf;
}

inline std::string count(const uint32_t value) {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
  return buf;
}

}  // namespace statsformat
