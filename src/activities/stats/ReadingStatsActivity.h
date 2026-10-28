#pragma once

#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "activities/UiListActivity.h"

// Reading statistics: the totals across every book, a bar chart of the last
// week, and the per-book breakdown.
//
// One list, two sections: a block of summary rows, then one row per book
// ordered by time spent. Tapping a book opens BookStatsActivity, which is where
// the per-book figures (progress, what is left, when it was started) live --
// they need a screen of their own, not a third column on a 480px panel.
//
// The chart lives in the chrome above the list, where it is drawn once per
// repaint instead of competing with row layout.
//
// The whole table is loaded in onEnter and held while the screen is open: it is
// bounded by the store (128 books, 90 days -> under 10KB), the screen owns the
// foreground while it is up, and the alternative -- re-reading rows from SD per
// repaint -- would make scrolling pay SD latency for data that never changes.
class ReadingStatsActivity final : public UiListActivity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReadingStats", renderer, mappedInput) {}

  void onEnter() override;

 private:
  // Bars drawn above the list. A week reads clearly at this width; a month of
  // bars on a 480px panel would be a few pixels each.
  static constexpr int CHART_DAYS = 7;

  std::vector<ReadingStatsStore::BookStat> books;
  std::vector<ReadingStatsStore::DayStat> days;
  ReadingStatsStore::Totals totals;

  // Row strings are built once (data never changes while the screen is open)
  // and reused by every repaint, so a repaint allocates nothing.
  std::vector<std::string> rowLabels;
  std::vector<std::string> rowValues;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;
  // Index into `books` for the book rows, -1 for section headings and summary
  // rows. Activation reads this rather than subtracting a row count, which
  // would have to be kept in step with every change to the summary block.
  std::vector<int16_t> rowBook;
  void buildRows();

  // Seconds read on each of the last CHART_DAYS days, oldest first, plus the
  // day number each slot stands for (0 when the slot predates the epoch).
  void collectChartDays(uint32_t seconds[CHART_DAYS], uint16_t dayNumbers[CHART_DAYS], uint32_t& peakOut) const;
  uint32_t secondsSince(uint16_t firstDay) const;
  // Height of the band drawChrome paints between the header and the list, so
  // buildScreen can reserve exactly what is drawn. 0 when there is no band.
  int chartBandHeight() const;
  bool hasDayData() const;

  static std::string formatDuration(uint32_t seconds);

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
};
