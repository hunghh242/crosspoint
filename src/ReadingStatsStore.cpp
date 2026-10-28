#include "ReadingStatsStore.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr uint32_t STATS_MAGIC = 0x54535043;  // "CPST"
// v2 added the per-book progress pair (startBp/progressBp). A v1 file is not
// upgraded in place: those two fields are exactly what the per-book screen is
// built on, and a few days of totals is a cheap thing to lose once.
constexpr uint8_t STATS_VERSION = 2;
// Days count from 2020-01-01 so a uint16 covers far past any plausible device
// lifetime while keeping a daily bucket at 8 bytes.
constexpr int64_t DAY_EPOCH_2020 = 18262;  // days from 1970-01-01 to 2020-01-01
// Below this a speed figure is noise: opening a book and closing it again would
// otherwise read as thousands of pages per hour.
constexpr uint32_t MIN_SECONDS_FOR_SPEED = 600;
constexpr uint32_t MIN_PAGES_FOR_SPEED = 10;
// A projection needs a span of progress wide enough that rounding in the
// reader's own percentage does not dominate it: 2% of a book.
constexpr uint16_t MIN_SPAN_BP_FOR_PROJECTION = 200;

// Days from 1970-01-01 for a civil date (Howard Hinnant's algorithm). Shifts
// the year to start in March so leap days land at the end of the cycle.
int64_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const auto yoe = static_cast<unsigned>(year - era * 400);                       // [0, 399]
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;  // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                     // [0, 146096]
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil, for dating a stored day number.
void civilFromDays(int64_t z, int& yearOut, int& monthOut, int& dayOut) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const auto doe = static_cast<unsigned>(z - era * 146097);                    // [0, 146096]
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
  const unsigned mp = (5 * doy + 2) / 153;                       // [0, 11]
  dayOut = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);       // [1, 31]
  monthOut = static_cast<int>(mp + (mp < 10 ? 3 : -9));          // [1, 12]
  yearOut = static_cast<int>(y + (monthOut <= 2 ? 1 : 0));
}

// The progress a book has been observed to cover, in basis points. Drives both
// projections, so the clamping lives in one place.
uint16_t timedSpanBp(const ReadingStatsStore::BookStat& book) {
  if (book.progressBp <= book.startBp) return 0;
  return static_cast<uint16_t>(book.progressBp - book.startBp);
}
}  // namespace

const char* ReadingStatsStore::filePath() { return "/.crosspoint/stats.bin"; }

uint16_t ReadingStatsStore::today() {
  if (!halClock.isAvailable()) return 0;
  struct tm local = {};
  if (!halClock.localTime(local)) return 0;
  // Civil date to a day number directly, rather than through timegm(): newlib
  // on this target has no timegm, and going via a timestamp would re-apply the
  // timezone that localTime() just took off.
  const int64_t days = daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) - DAY_EPOCH_2020;
  if (days <= 0 || days > UINT16_MAX) return 0;
  return static_cast<uint16_t>(days);
}

void ReadingStatsStore::civilFromDay(const uint16_t day, int& yearOut, int& monthOut, int& dayOut) {
  civilFromDays(static_cast<int64_t>(day) + DAY_EPOCH_2020, yearOut, monthOut, dayOut);
}

uint8_t ReadingStatsStore::weekdayOfDay(const uint16_t day) {
  // 1970-01-01 was a Thursday, i.e. index 3 counting Monday as 0.
  const int64_t abs = static_cast<int64_t>(day) + DAY_EPOCH_2020;
  return static_cast<uint8_t>(((abs + 3) % 7 + 7) % 7);
}

uint32_t ReadingStatsStore::hashPath(const std::string& path) {
  // FNV-1a: the identity only has to be stable and collision-unlikely across a
  // card's worth of paths, and it allocates nothing.
  uint32_t h = 2166136261u;
  for (const char c : path) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

bool ReadingStatsStore::readAll(std::vector<BookStat>& books, std::vector<DayStat>& days) {
  books.clear();
  days.clear();
  HalFile f;
  if (!Storage.openFileForRead("STATS", filePath(), f)) return false;  // first run

  uint32_t magic = 0;
  uint8_t version = 0;
  serialization::readPod(f, magic);
  serialization::readPod(f, version);
  if (magic != STATS_MAGIC || version != STATS_VERSION) {
    LOG_ERR("STATS", "Unrecognised stats file (magic %08lx v%u); starting over", static_cast<unsigned long>(magic),
            version);
    return false;
  }

  uint16_t dayCount = 0;
  uint16_t bookCount = 0;
  serialization::readPod(f, dayCount);
  serialization::readPod(f, bookCount);
  if (dayCount > MAX_DAYS || bookCount > MAX_BOOKS) {
    LOG_ERR("STATS", "Stats counts out of range (%u days, %u books)", dayCount, bookCount);
    return false;
  }

  days.reserve(dayCount);
  for (uint16_t i = 0; i < dayCount; i++) {
    DayStat d;
    serialization::readPod(f, d.day);
    serialization::readPod(f, d.seconds);
    serialization::readPod(f, d.pages);
    days.push_back(d);
  }
  books.reserve(bookCount);
  for (uint16_t i = 0; i < bookCount; i++) {
    BookStat b;
    serialization::readPod(f, b.pathHash);
    serialization::readPod(f, b.seconds);
    serialization::readPod(f, b.pages);
    serialization::readPod(f, b.firstDay);
    serialization::readPod(f, b.lastDay);
    serialization::readPod(f, b.startBp);
    serialization::readPod(f, b.progressBp);
    if (f.read(reinterpret_cast<uint8_t*>(b.title), TITLE_CAP) != static_cast<int>(TITLE_CAP)) {
      LOG_ERR("STATS", "Truncated stats file at book %u", i);
      return false;
    }
    b.title[TITLE_CAP - 1] = 0;
    books.push_back(b);
  }
  return true;
}

bool ReadingStatsStore::writeAll(const std::vector<BookStat>& books, const std::vector<DayStat>& days) {
  HalFile f;
  if (!Storage.openFileForWrite("STATS", filePath(), f)) {
    LOG_ERR("STATS", "Cannot open %s for write", filePath());
    return false;
  }
  serialization::writePod(f, STATS_MAGIC);
  serialization::writePod(f, STATS_VERSION);
  serialization::writePod(f, static_cast<uint16_t>(days.size()));
  serialization::writePod(f, static_cast<uint16_t>(books.size()));
  for (const auto& d : days) {
    serialization::writePod(f, d.day);
    serialization::writePod(f, d.seconds);
    serialization::writePod(f, d.pages);
  }
  for (const auto& b : books) {
    serialization::writePod(f, b.pathHash);
    serialization::writePod(f, b.seconds);
    serialization::writePod(f, b.pages);
    serialization::writePod(f, b.firstDay);
    serialization::writePod(f, b.lastDay);
    serialization::writePod(f, b.startBp);
    serialization::writePod(f, b.progressBp);
    f.write(reinterpret_cast<const uint8_t*>(b.title), TITLE_CAP);
  }
  return true;
}

bool ReadingStatsStore::record(const std::string& path, const std::string& title, const uint32_t seconds,
                               const uint32_t pages, const uint16_t startBp, const uint16_t endBp) {
  if (seconds == 0 && pages == 0) return true;

  std::vector<BookStat> books;
  std::vector<DayStat> days;
  readAll(books, days);  // a missing or unreadable file just starts a fresh table

  const uint16_t day = today();
  const uint32_t hash = hashPath(path);

  auto it = std::find_if(books.begin(), books.end(), [hash](const BookStat& b) { return b.pathHash == hash; });
  if (it == books.end()) {
    if (books.size() >= MAX_BOOKS) {
      // Evict the least recently read: the file stays bounded, and what goes is
      // what the reader is least likely to miss.
      const auto oldest = std::min_element(books.begin(), books.end(),
                                           [](const BookStat& a, const BookStat& b) { return a.lastDay < b.lastDay; });
      books.erase(oldest);
    }
    books.push_back(BookStat{});
    it = books.end() - 1;
    it->pathHash = hash;
    it->firstDay = day;
    it->startBp = startBp;
  }
  it->seconds += seconds;
  it->pages += pages;
  it->lastDay = day;
  if (it->firstDay == 0) it->firstDay = day;
  // Progress only ever moves forward here. Re-reading an earlier chapter would
  // otherwise make the book look abandoned, and would make the projection
  // divide by a span the reader has in fact already covered.
  it->progressBp = std::max(it->progressBp, endBp);
  // Refresh the title every time: a book re-added with better metadata should
  // stop showing whatever it was first seen as.
  if (!title.empty()) {
    std::snprintf(it->title, TITLE_CAP, "%s", title.c_str());
  }

  if (day != 0) {
    auto dit = std::find_if(days.begin(), days.end(), [day](const DayStat& d) { return d.day == day; });
    if (dit == days.end()) {
      days.push_back(DayStat{day, 0, 0});
      // Keep the window to the most recent MAX_DAYS, oldest first.
      std::sort(days.begin(), days.end(), [](const DayStat& a, const DayStat& b) { return a.day < b.day; });
      if (days.size() > MAX_DAYS) days.erase(days.begin(), days.end() - MAX_DAYS);
      dit = std::find_if(days.begin(), days.end(), [day](const DayStat& d) { return d.day == day; });
    }
    if (dit != days.end()) {
      dit->seconds += seconds;
      dit->pages = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, dit->pages + pages));
    }
  }

  return writeAll(books, days);
}

bool ReadingStatsStore::load(std::vector<BookStat>& booksOut, std::vector<DayStat>& daysOut) {
  return readAll(booksOut, daysOut);
}

ReadingStatsStore::Totals ReadingStatsStore::totalsOf(const std::vector<BookStat>& books) {
  Totals t;
  for (const auto& b : books) {
    t.seconds += b.seconds;
    t.pages += b.pages;
    if (b.progressBp >= FINISHED_BP) t.finished++;
  }
  t.books = static_cast<uint16_t>(books.size());
  return t;
}

uint16_t ReadingStatsStore::pagesPerHour(const uint32_t seconds, const uint32_t pages) {
  if (seconds < MIN_SECONDS_FOR_SPEED || pages < MIN_PAGES_FOR_SPEED) return 0;
  const uint64_t perHour = (static_cast<uint64_t>(pages) * 3600u) / seconds;
  return static_cast<uint16_t>(std::min<uint64_t>(perHour, UINT16_MAX));
}

uint16_t ReadingStatsStore::streakDays(const std::vector<DayStat>& days) {
  const uint16_t day = today();
  if (day == 0 || days.empty()) return 0;
  const auto hasDay = [&days](const uint16_t d) {
    return std::any_of(days.begin(), days.end(), [d](const DayStat& x) { return x.day == d && x.seconds > 0; });
  };
  // Start at today, or yesterday when today has nothing yet: an evening reader
  // should not watch the streak zero itself every morning.
  uint16_t cursor = day;
  if (!hasDay(cursor)) {
    if (cursor == 0 || !hasDay(static_cast<uint16_t>(cursor - 1))) return 0;
    cursor = static_cast<uint16_t>(cursor - 1);
  }
  uint16_t streak = 0;
  while (cursor > 0 && hasDay(cursor)) {
    streak++;
    cursor = static_cast<uint16_t>(cursor - 1);
  }
  return streak;
}

uint32_t ReadingStatsStore::averageSecondsPerActiveDay(const std::vector<DayStat>& days) {
  uint32_t total = 0;
  uint32_t active = 0;
  for (const auto& d : days) {
    if (d.seconds == 0) continue;
    total += d.seconds;
    active++;
  }
  return active == 0 ? 0 : total / active;
}

uint32_t ReadingStatsStore::secondsRemaining(const BookStat& book) {
  const uint16_t span = timedSpanBp(book);
  if (span < MIN_SPAN_BP_FOR_PROJECTION || book.progressBp >= FINISHED_BP || book.seconds == 0) return 0;
  const uint32_t left = 10000u - book.progressBp;
  return static_cast<uint32_t>((static_cast<uint64_t>(book.seconds) * left) / span);
}

uint32_t ReadingStatsStore::pagesRemaining(const BookStat& book) {
  const uint16_t span = timedSpanBp(book);
  if (span < MIN_SPAN_BP_FOR_PROJECTION || book.progressBp >= FINISHED_BP || book.pages == 0) return 0;
  const uint32_t left = 10000u - book.progressBp;
  return static_cast<uint32_t>((static_cast<uint64_t>(book.pages) * left) / span);
}

uint32_t ReadingStatsStore::projectedTotalPages(const BookStat& book) {
  const uint16_t span = timedSpanBp(book);
  if (span < MIN_SPAN_BP_FOR_PROJECTION || book.pages == 0) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(book.pages) * 10000u) / span);
}
