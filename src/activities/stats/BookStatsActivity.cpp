#include "BookStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "StatsFormat.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int BAR_HEIGHT = 10;
constexpr int BAND_PADDING = 6;
}  // namespace

int BookStatsActivity::progressBandHeight() const { return BAR_HEIGHT + BAND_PADDING * 2; }

void BookStatsActivity::buildRows() {
  rowLabels.clear();
  rowValues.clear();
  rowItems.clear();

  constexpr size_t capacity = 13;
  rowLabels.reserve(capacity);
  rowValues.reserve(capacity);

  char buf[48];
  std::vector<bool> headings;
  headings.reserve(capacity);
  const auto add = [&](const char* label, std::string value) {
    rowLabels.emplace_back(label);
    rowValues.emplace_back(std::move(value));
    headings.push_back(false);
  };
  const auto addHeading = [&](const char* label) {
    rowLabels.emplace_back(label);
    rowValues.emplace_back();
    headings.push_back(true);
  };

  const uint16_t speed = ReadingStatsStore::pagesPerHour(book.seconds, book.pages);
  const uint32_t projectedPages = ReadingStatsStore::projectedTotalPages(book);
  const uint32_t secondsLeft = ReadingStatsStore::secondsRemaining(book);
  const uint32_t pagesLeft = ReadingStatsStore::pagesRemaining(book);
  const bool finished = book.progressBp >= ReadingStatsStore::FINISHED_BP;

  addHeading(tr(STR_STATS_SECTION_PROGRESS));
  add(tr(STR_STATS_PROGRESS), finished ? tr(STR_STATS_DONE) : statsformat::percent(book.progressBp));
  if (book.startBp > 0) {
    // Only worth a row when the book did not start at zero: it explains why the
    // projections below divide by less than the whole book.
    add(tr(STR_STATS_TRACKED_FROM), statsformat::percent(book.startBp));
  }
  add(tr(STR_STATS_PAGES), statsformat::count(book.pages));
  add(tr(STR_STATS_TOTAL_TIME), statsformat::duration(book.seconds));
  if (speed > 0) {
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_SPEED_FMT), speed);
    add(tr(STR_STATS_SPEED), buf);
  } else {
    add(tr(STR_STATS_SPEED), tr(STR_STATS_NOT_ENOUGH));
  }

  addHeading(tr(STR_STATS_SECTION_PROJECTION));
  if (finished) {
    add(tr(STR_STATS_TIME_LEFT), tr(STR_STATS_DONE));
    add(tr(STR_STATS_PAGES_LEFT), tr(STR_STATS_DONE));
  } else if (secondsLeft == 0 && pagesLeft == 0) {
    // Not enough of this book has been timed to divide by; a projection from a
    // single session would be off by whatever that session happened to be.
    add(tr(STR_STATS_TIME_LEFT), tr(STR_STATS_NOT_ENOUGH));
    add(tr(STR_STATS_PAGES_LEFT), tr(STR_STATS_NOT_ENOUGH));
  } else {
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_APPROX_FMT), statsformat::duration(secondsLeft).c_str());
    add(tr(STR_STATS_TIME_LEFT), buf);
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_APPROX_FMT), statsformat::count(pagesLeft).c_str());
    add(tr(STR_STATS_PAGES_LEFT), buf);
  }
  if (projectedPages > 0) {
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_APPROX_FMT), statsformat::count(projectedPages).c_str());
    add(tr(STR_STATS_BOOK_LENGTH), buf);
  }

  addHeading(tr(STR_STATS_SECTION_HISTORY));
  add(tr(STR_STATS_FIRST_READ), statsformat::date(book.firstDay));
  add(tr(STR_STATS_LAST_READ), statsformat::date(book.lastDay));
  if (book.firstDay != 0 && book.lastDay >= book.firstDay) {
    std::snprintf(buf, sizeof(buf), tr(STR_STATS_DAYS_FMT),
                  static_cast<unsigned>(book.lastDay - book.firstDay + 1));
    add(tr(STR_STATS_SPAN), buf);
  }

  rowItems.reserve(rowLabels.size());
  for (size_t i = 0; i < rowLabels.size(); i++) {
    fui::ListItem item;
    item.label = rowLabels[i].c_str();
    if (headings[i]) {
      item.isHeader = true;
    } else {
      // Left enabled although nothing opens: item.enabled false also dims the
      // row, and a screen of greyed figures reads as stale data.
      item.value = rowValues[i].c_str();
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void BookStatsActivity::onEnter() {
  UiListActivity::onEnter();
  title = book.title[0] != 0 ? book.title : tr(STR_LIBRARY_UNKNOWN_TITLE);
  buildRows();
  requestUpdate();
}

void BookStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + progressBandHeight()),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void BookStatsActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), title.c_str());

  // The bar repeats the progress row on purpose: a percentage is a number to
  // read, a bar is a position to see, and this screen is about where in the
  // book the reader is.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int barTop = safe.y + metrics.topPadding + metrics.headerHeight + BAND_PADDING;
  GUI.drawProgressBar(renderer, Rect{safe.x, barTop, safe.width, BAR_HEIGHT}, book.progressBp, 10000);
}
