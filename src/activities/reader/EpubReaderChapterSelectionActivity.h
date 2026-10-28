#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  int currentTocIndex = 0;

  // Windowed row buffers: TOC entries are SD-backed (BookMetadataCache LUT
  // reads), so only the rows around the viewport are materialized. A
  // several-hundred-entry TOC (547 in a large collection) built up front cost
  // ~60KB of labels + ListItems — starving the CJK glyph arena into
  // SD-per-repaint — for rows that were never drawn. The window follows
  // nav.top via itemsWindowFirst (see fui::ListProps); refreshing it also
  // batch-prewarms the window's fallback glyphs, so each page of the list
  // pays one bounded SD pass and repaints stay RAM-only.
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  freeink::ui::ListItem windowItems[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start);

  // A flattened TOC keeps its nesting as a level per entry, in document order,
  // so an entry's children are the run of deeper entries that follows it. Books
  // that use that nesting list their parts first and open one part at a time --
  // a 300-chapter collection is unusable as one flat list. Books with no
  // nesting keep exactly the single list they always had.
  std::vector<uint8_t> tocLevels;    // one per TOC entry; empty = treat as flat
  std::vector<int16_t> visibleRows;  // TOC indices listed now; unused when flat
  bool hierarchical = false;
  int openParent = -1;    // TOC index of the open part, -1 = at the top
  std::string openTitle;  // header text while a part is open
  int tocIndexForRow(int row) const;
  // One past the last descendant of `index` (its subtree end).
  int subtreeEnd(int index) const;
  bool hasChildren(int index) const;
  // No shallower entry precedes it, so it belongs at the top of the list.
  bool isRoot(int index) const;
  // Rebuild visibleRows for the current openParent and reset the row window.
  void buildVisibleRows();
  void openPart(int tocIndex);
  // Returns false at the top level, where Back leaves the screen as before.
  bool closePart();

  int listCount() const override {
    if (!epub) return 0;
    return hierarchical ? static_cast<int>(visibleRows.size()) : epub->getTocItemsCount();
  }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Back cancels with a result and Confirm activates on RELEASE here, and a
  // missing epub swallows everything past Back.
  bool handleButtons() override;
  // Header is drawn inside the safe area (not full-width like the base).
  void drawChrome() override;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, int currentTocIndex);
  void onEnter() override;
};
