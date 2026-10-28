#include "ReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "BookStatsActivity.h"
#include "StatsFormat.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Height of the bars themselves; the band around them is measured from the
// font so the labels never collide on a board with a larger UI font.
constexpr int CHART_PLOT_HEIGHT = 46;
constexpr int CHART_BAR_GAP = 6;
// A day with any reading at all still gets a visible stub, so an hour next to
// five minutes reads as "both days, very different" rather than "one day".
constexpr int CHART_MIN_BAR = 2;
constexpr int CHART_PADDING = 4;

// Pulls the i-th comma-separated field out of a packed translation. The weekday
// initials ship as one string ("Mo,Tu,...") rather than seven: seven StrIds for
// seven two-letter labels is a lot of table for one chart axis.
std::string packedField(const char* packed, const int index) {
  if (packed == nullptr) return {};
  const char* cursor = packed;
  for (int i = 0; i < index; i++) {
    const char* comma = std::strchr(cursor, ',');
    if (comma == nullptr) return {};
    cursor = comma + 1;
  }
  const char* end = std::strchr(cursor, ',');
  return end == nullptr ? std::string(cursor) : std::string(cursor, static_cast<size_t>(end - cursor));
}
}  // namespace

uint32_t ReadingStatsActivity::secondsSince(const uint16_t firstDay) const {
  uint32_t total = 0;
  for (const auto& d : days) {
    if (d.day >= firstDay) total += d.seconds;
  }
  return total;
}

bool ReadingStatsActivity::hasDayData() const {
  return std::any_of(days.begin(), days.end(), [](const ReadingStatsStore::DayStat& d) { return d.seconds > 0; });
}

int ReadingStatsActivity::chartBandHeight() const {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  // No clock means no day buckets were ever written, so there is nothing to
  // chart. Say so in the band instead of leaving a gap the reader has to
  // interpret -- "where is my chart" is exactly the wrong question to provoke.
  if (ReadingStatsStore::today() == 0) return lineHeight + CHART_PADDING * 2;
  if (!hasDayData()) return 0;
  // Peak label, bars, baseline rule, weekday initials.
  return lineHeight * 2 + CHART_PLOT_HEIGHT + 1 + CHART_PADDING * 3;
}

void ReadingStatsActivity::collectChartDays(uint32_t seconds[CHART_DAYS], uint16_t dayNumbers[CHART_DAYS],
                                            uint32_t& peakOut) const {
  peakOut = 0;
  const uint16_t today = ReadingStatsStore::today();
  for (int i = 0; i < CHART_DAYS; i++) {
    seconds[i] = 0;
    // Oldest on the left, today on the right.
    const int back = CHART_DAYS - 1 - i;
    dayNumbers[i] = today > back ? static_cast<uint16_t>(today - back) : 0;
  }
  if (today == 0) return;
  for (const auto& d : days) {
    const int slot = CHART_DAYS - 1 - static_cast<int>(today - d.day);
    if (d.day <= today && slot >= 0 && slot < CHART_DAYS) {
      seconds[slot] = d.seconds;
      peakOut = std::max(peakOut, d.seconds);
    }
  }
}

void ReadingStatsActivity::buildRows() {
  rowLabels.clear();
  rowValues.clear();
  rowSubtitles.clear();
  rowItems.clear();
  rowBook.clear();

  // Two section headings, eight summary rows, one row per book.
  const size_t capacity = 10 + books.size();
  rowLabels.reserve(capacity);
  rowValues.reserve(capacity);
  rowSubtitles.reserve(capacity);
  rowBook.reserve(capacity);

  const uint16_t today = ReadingStatsStore::today();
  const uint16_t speed = ReadingStatsStore::pagesPerHour(totals.seconds, totals.pages);
  const uint16_t streak = ReadingStatsStore::streakDays(days);

  char buf[48];
  const auto add = [&](const char* label, std::string value, std::string subtitle = {}) {
    rowLabels.emplace_back(label);
    rowValues.emplace_back(std::move(value));
    rowSubtitles.emplace_back(std::move(subtitle));
    rowBook.push_back(-1);
  };
  const auto addHeading = [&](const char* label) {
    rowLabels.emplace_back(label);
    rowValues.emplace_back();
    rowSubtitles.emplace_back();
    rowBook.push_back(-2);  // -2 marks a non-interactive section heading
  };

  addHeading(tr(STR_STATS_SECTION_OVERVIEW));
  add(tr(STR_STATS_TOTAL_TIME), statsformat::duration(totals.seconds));
  add(tr(STR_STATS_PAGES), statsformat::count(totals.pages));
  if (speed > 0) {
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_SPEED_FMT), speed);
    add(tr(STR_STATS_SPEED), buf);
  } else {
    // Too little read for the figure to mean anything; saying so beats showing
    // a number derived from a few seconds.
    add(tr(STR_STATS_SPEED), tr(STR_STATS_NOT_ENOUGH));
  }
  std::snprintf(buf, sizeof(buf), "%u / %u", static_cast<unsigned>(totals.finished),
                static_cast<unsigned>(totals.books));
  add(tr(STR_STATS_BOOKS_FINISHED), buf);

  if (today != 0) {
    add(tr(STR_STATS_TODAY), statsformat::duration(secondsSince(today)));
    add(tr(STR_STATS_WEEK), statsformat::duration(secondsSince(static_cast<uint16_t>(today >= 6 ? today - 6 : 0))));
    add(tr(STR_STATS_AVG_DAY), statsformat::duration(ReadingStatsStore::averageSecondsPerActiveDay(days)));
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_DAYS_FMT), streak);
    add(tr(STR_STATS_STREAK), buf);
  } else {
    // No RTC, or the clock was never set: the per-day buckets were never
    // written, so say why rather than showing four zeroes.
    add(tr(STR_STATS_TODAY), tr(STR_STATS_NO_CLOCK));
    add(tr(STR_STATS_WEEK), tr(STR_STATS_NO_CLOCK));
    add(tr(STR_STATS_AVG_DAY), tr(STR_STATS_NO_CLOCK));
    add(tr(STR_STATS_STREAK), tr(STR_STATS_NO_CLOCK));
  }

  if (!books.empty()) {
    addHeading(tr(STR_STATS_SECTION_BOOKS));
    for (size_t i = 0; i < books.size(); i++) {
      const auto& b = books[i];
      rowLabels.emplace_back(b.title[0] != 0 ? b.title : tr(STR_LIBRARY_UNKNOWN_TITLE));
      rowValues.emplace_back(statsformat::duration(b.seconds));
      // The progress and page count go on the subtitle rather than into the
      // value slot: the value column is where the eye compares like with like,
      // and mixing a duration with a percentage there makes both harder to read.
      std::snprintf(buf, sizeof(buf), tr(STR_STATS_BOOK_SUBTITLE_FMT), static_cast<unsigned>((b.progressBp + 50) / 100),
                    static_cast<unsigned long>(b.pages));
      rowSubtitles.emplace_back(buf);
      rowBook.push_back(static_cast<int16_t>(i));
    }
  }

  // Pointers are taken after every vector is fully grown: reserve() alone does
  // not stop emplace_back from reallocating if the count were ever misjudged.
  rowItems.reserve(rowLabels.size());
  for (size_t i = 0; i < rowLabels.size(); i++) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    if (rowBook[i] == -2) {
      item.isHeader = true;
    } else {
      item.value = rowValues[i].c_str();
      if (!rowSubtitles[i].empty()) item.subtitle = rowSubtitles[i].c_str();
      // Summary rows stay enabled even though they open nothing: item.enabled
      // false also dims the row (StateDisabled), and greyed-out figures would
      // read as stale data rather than as "not a button".
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ReadingStatsActivity::onEnter() {
  UiListActivity::onEnter();

  READING_STATS.load(books, days);
  // Most-read first: the book someone has actually spent time in is the one
  // they look for.
  std::sort(books.begin(), books.end(), [](const ReadingStatsStore::BookStat& a,
                                           const ReadingStatsStore::BookStat& b) { return a.seconds > b.seconds; });
  totals = ReadingStatsStore::totalsOf(books);
  buildRows();
  requestUpdate();
}

void ReadingStatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rowBook.size())) return;
  const int16_t bookIndex = rowBook[index];
  if (bookIndex < 0 || bookIndex >= static_cast<int16_t>(books.size())) return;
  app.clearTapFlash();
  startActivityForResult(std::make_unique<BookStatsActivity>(renderer, mappedInput, books[bookIndex]),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content starts below the header and the chart band drawChrome paints into.
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + chartBandHeight()),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems.empty()) {
    screen.centeredText(tr(STR_STATS_EMPTY), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in the base loop
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReadingStatsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), tr(STR_STATISTICS));

  const int bandHeight = chartBandHeight();
  if (bandHeight == 0) return;

  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int bandTop = safe.y + metrics.topPadding + metrics.headerHeight;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  if (ReadingStatsStore::today() == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, bandTop + CHART_PADDING, tr(STR_STATS_NO_CLOCK_CHART));
    return;
  }

  uint32_t perDay[CHART_DAYS];
  uint16_t dayNumbers[CHART_DAYS];
  uint32_t peak = 0;
  collectChartDays(perDay, dayNumbers, peak);
  if (peak == 0) return;

  // Peak label: the bars are scaled against the week's own maximum, so without
  // it the chart shows the shape of the week but not its size.
  char peakLabel[32];
  std::snprintf(peakLabel, sizeof(peakLabel), tr(STR_STATS_CHART_PEAK_FMT), statsformat::duration(peak).c_str());
  const int peakWidth = renderer.getTextWidth(UI_10_FONT_ID, peakLabel);
  renderer.drawText(UI_10_FONT_ID, safe.x + safe.width - peakWidth, bandTop + CHART_PADDING, peakLabel);

  const int plotTop = bandTop + CHART_PADDING + lineHeight + CHART_PADDING;
  const int barSlot = safe.width / CHART_DAYS;
  const int barWidth = std::max(1, barSlot - CHART_BAR_GAP);
  const char* weekdays = tr(STR_STATS_WEEKDAYS);

  for (int i = 0; i < CHART_DAYS; i++) {
    const int x = safe.x + i * barSlot + (barSlot - barWidth) / 2;
    if (perDay[i] > 0) {
      int height = static_cast<int>((static_cast<uint64_t>(perDay[i]) * CHART_PLOT_HEIGHT) / peak);
      height = std::clamp(height, CHART_MIN_BAR, CHART_PLOT_HEIGHT);
      renderer.fillRect(x, plotTop + CHART_PLOT_HEIGHT - height, barWidth, height, true);
    }
    // Weekday initial centred under its slot, so a glance tells you which day
    // the tall bar was.
    const std::string label = packedField(weekdays, static_cast<int>(ReadingStatsStore::weekdayOfDay(dayNumbers[i])));
    if (!label.empty()) {
      const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
      renderer.drawText(UI_10_FONT_ID, x + (barWidth - labelWidth) / 2, plotTop + CHART_PLOT_HEIGHT + 1 + CHART_PADDING,
                        label.c_str());
    }
  }
  // Baseline: without it the bars float, and an empty day reads as a gap in
  // nothing rather than a day with no reading.
  renderer.fillRect(safe.x, plotTop + CHART_PLOT_HEIGHT, safe.width, 1, true);
}
