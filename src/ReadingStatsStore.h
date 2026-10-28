#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reading statistics: how long and how much has actually been read.
//
// Deliberately a compact binary file rather than the JSON PersistableStore the
// settings use. The table is a few thousand records' worth of fixed-size rows,
// and parsing that into an elastic JsonDocument would put several KB on a heap
// whose fragmentation is this firmware's main failure mode. Same shape as the
// other data this project keeps on SD (progress.bin, book.bin).
//
// Books are keyed by a hash of their path, so a record survives the book being
// renamed in the reader's view -- and is kept when the file is deleted, since
// "how much have I read" is a history, not an inventory.
class ReadingStatsStore {
 public:
  // Title is stored with the record: a deleted book is still listed, long after
  // the library index that could have named it is gone.
  static constexpr size_t TITLE_CAP = 48;
  // One SD file, bounded: 128 books (8.7KB) plus 90 daily buckets (~0.7KB).
  // Beyond that the least recently read book is dropped, so the file can never
  // grow without limit on a device that is also short of space.
  static constexpr size_t MAX_BOOKS = 128;
  static constexpr size_t MAX_DAYS = 90;
  // At or above this the book counts as finished. Not 10000: the last page of a
  // book is often a colophon the reader never turns onto.
  static constexpr uint16_t FINISHED_BP = 9800;

  struct BookStat {
    uint32_t pathHash = 0;
    uint32_t seconds = 0;
    uint32_t pages = 0;
    uint16_t firstDay = 0;  // days since 2020-01-01; 0 = unknown (no RTC)
    uint16_t lastDay = 0;
    // Progress (basis points) when this book was first recorded and as of the
    // last session. The pair is what makes a projection honest: a book imported
    // at 40% has only had 60% of its pages timed, and the remaining-time
    // estimate must divide by the span actually read, not by the whole book.
    uint16_t startBp = 0;
    uint16_t progressBp = 0;
    char title[TITLE_CAP] = {};
  };

  struct DayStat {
    uint16_t day = 0;
    uint32_t seconds = 0;
    uint16_t pages = 0;
  };

  static ReadingStatsStore& getInstance() {
    static ReadingStatsStore instance;
    return instance;
  }

  // Days since 2020-01-01 from the RTC, or 0 when there is no readable clock.
  // 0 doubles as "unknown", which the daily buckets skip and the totals ignore.
  static uint16_t today();
  // Civil date of a day number, for dating a book's first and last session.
  static void civilFromDay(uint16_t day, int& yearOut, int& monthOut, int& dayOut);
  // 0 = Monday .. 6 = Sunday, for labelling the chart.
  static uint8_t weekdayOfDay(uint16_t day);
  static uint32_t hashPath(const std::string& path);

  // Fold one reading session into the file: read, merge, write. Called rarely
  // (reader exit, sleep, a periodic flush), never per page turn.
  bool record(const std::string& path, const std::string& title, uint32_t seconds, uint32_t pages, uint16_t startBp,
              uint16_t endBp);

  // Load everything for the statistics screen. Held only while that screen is
  // open; the reader never needs the table.
  bool load(std::vector<BookStat>& booksOut, std::vector<DayStat>& daysOut);

  // Aggregates over every record, including books no longer on the card.
  struct Totals {
    uint32_t seconds = 0;
    uint32_t pages = 0;
    uint16_t books = 0;
    uint16_t finished = 0;
  };
  static Totals totalsOf(const std::vector<BookStat>& books);
  // Reading speed in pages per hour, 0 when too little has been read to mean
  // anything (a few seconds of open-and-close would read as thousands).
  static uint16_t pagesPerHour(uint32_t seconds, uint32_t pages);
  // Consecutive days ending today (or yesterday, so an evening reader does not
  // watch the streak reset at midnight before they pick the book up).
  static uint16_t streakDays(const std::vector<DayStat>& days);
  // Mean seconds over the days that have a bucket, i.e. the days actually read
  // on -- not calendar days, which would read as a reproach for taking a break.
  static uint32_t averageSecondsPerActiveDay(const std::vector<DayStat>& days);

  // Projections from a book's own timed span. 0 when the span is too short to
  // divide by (a book opened once, or imported already finished).
  static uint32_t secondsRemaining(const BookStat& book);
  static uint32_t pagesRemaining(const BookStat& book);
  // Pages the whole book is projected to hold, 0 when not yet projectable.
  static uint32_t projectedTotalPages(const BookStat& book);

 private:
  ReadingStatsStore() = default;
  static const char* filePath();
  static bool readAll(std::vector<BookStat>& books, std::vector<DayStat>& days);
  static bool writeAll(const std::vector<BookStat>& books, const std::vector<DayStat>& days);
};

#define READING_STATS ReadingStatsStore::getInstance()
