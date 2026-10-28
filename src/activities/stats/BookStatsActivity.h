#pragma once

#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "activities/UiListActivity.h"

// Per-book reading statistics: where the reader is in this book, what it has
// cost them so far, and what finishing it is projected to take.
//
// Holds its own copy of the record (72 bytes) rather than a pointer into the
// overview's vector: the overview stays on the stack below, but a record that
// is only read is cheaper to copy than to keep valid.
//
// Every projection on this screen is derived from the span of the book this
// device has actually timed -- see ReadingStatsStore::secondsRemaining. A book
// imported at 60% has only 40% of its pages on the clock, and a figure that
// pretended otherwise would be wrong by the same factor for the whole book.
class BookStatsActivity final : public UiListActivity {
 public:
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const ReadingStatsStore::BookStat& book)
      : UiListActivity("BookStats", renderer, mappedInput), book(book) {}

  void onEnter() override;

 private:
  ReadingStatsStore::BookStat book;
  std::string title;

  std::vector<std::string> rowLabels;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;
  void buildRows();

  // Height of the progress-bar band drawChrome paints under the header.
  int progressBandHeight() const;

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override {}  // nothing to open: this screen is a report
  void drawChrome() override;
};
