#include "EpubReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

EpubReaderChapterSelectionActivity::EpubReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const std::shared_ptr<Epub>& epub,
                                                                       const int currentTocIndex)
    : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
      epub(epub),
      currentTocIndex(currentTocIndex) {}

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  // The reader underneath pins its page-render glyph arenas while this
  // overlay is up. clearCache() is heap-adaptive: below the retention floor
  // it frees them (the next page render's PrewarmScope rebuilds them at
  // normal page-turn cost), giving this list room to keep every row's
  // fallback glyphs resident — otherwise each repaint re-reads the visible
  // rows' glyphs from SD.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  if (!epub) {
    return;
  }

  // One sequential SD pass for the nesting levels; the rows themselves stay
  // windowed. Only worth anything when the book actually nests.
  tocLevels = epub->getTocLevels();
  if (tocLevels.size() == static_cast<size_t>(epub->getTocItemsCount()) && !tocLevels.empty()) {
    // Nested only if something actually hangs off something else.
    hierarchical =
        std::any_of(tocLevels.begin(), tocLevels.end(),
                    [min = *std::min_element(tocLevels.begin(), tocLevels.end())](uint8_t l) { return l > min; });
  }

  if (hierarchical) {
    // Open on the part holding the current chapter, with that chapter selected,
    // so Contents lands where the reader is instead of at the top of the book.
    const int current =
        currentTocIndex >= 0 && currentTocIndex < static_cast<int>(tocLevels.size()) ? currentTocIndex : -1;
    if (current >= 0 && !isRoot(current)) {
      // Nearest shallower entry before it is its parent.
      for (int i = current - 1; i >= 0; i--) {
        if (tocLevels[i] < tocLevels[current]) {
          openParent = i;
          openTitle = epub->getTocItem(i).title;
          break;
        }
      }
    }
    buildVisibleRows();
    for (size_t row = 0; row < visibleRows.size(); row++) {
      if (visibleRows[row] == current) {
        nav.selected = static_cast<int>(row);
        break;
      }
    }
    return;
  }

  // Start with the current chapter at the top of the viewport; the first
  // screen build pulls the viewport to it (ListNav follow-on-build) and
  // materializes the row window there (refreshTocWindow in buildScreen).
  nav.selected = currentTocIndex;
}

int EpubReaderChapterSelectionActivity::tocIndexForRow(const int row) const {
  if (!hierarchical) return row;
  if (row < 0 || row >= static_cast<int>(visibleRows.size())) return -1;
  return visibleRows[row];
}

int EpubReaderChapterSelectionActivity::subtreeEnd(const int index) const {
  const int total = static_cast<int>(tocLevels.size());
  for (int i = index + 1; i < total; i++) {
    if (tocLevels[i] <= tocLevels[index]) return i;
  }
  return total;
}

bool EpubReaderChapterSelectionActivity::hasChildren(const int index) const {
  const int next = index + 1;
  return next < static_cast<int>(tocLevels.size()) && tocLevels[next] > tocLevels[index];
}

// An entry with no shallower entry anywhere before it. Real TOCs are not tidy:
// front matter often sits deeper than the parts that follow, and some books
// jump straight from level 1 to level 3. Picking roots by "has no parent"
// rather than "equals the shallowest level" keeps every entry reachable --
// filtering on the level alone silently hides anything deeper that appears
// before the first top-level entry.
bool EpubReaderChapterSelectionActivity::isRoot(const int index) const {
  for (int i = 0; i < index; i++) {
    if (tocLevels[i] < tocLevels[index]) return false;
  }
  return true;
}

// Rows for the current level: the parts at the top, or one part's direct
// children once it is open. Only direct children -- a deeper entry opens in
// turn, so arbitrarily nested books stay one tap per level.
void EpubReaderChapterSelectionActivity::buildVisibleRows() {
  visibleRows.clear();
  const int total = static_cast<int>(tocLevels.size());
  if (openParent < 0) {
    for (int i = 0; i < total; i++) {
      if (isRoot(i)) visibleRows.push_back(static_cast<int16_t>(i));
    }
  } else {
    // The part's own target leads it. A part usually points at its title page,
    // which is a real destination and would otherwise be unreachable: tapping
    // the part opens its chapters and never jumps to it.
    if (epub->getTocItem(openParent).spineIndex != -1) {
      visibleRows.push_back(static_cast<int16_t>(openParent));
    }
    const int end = subtreeEnd(openParent);
    // Direct children are the shallowest entries inside the subtree, which is
    // not always parent level + 1: a TOC that skips a level would list none.
    uint8_t childLevel = UINT8_MAX;
    for (int i = openParent + 1; i < end; i++) {
      if (tocLevels[i] < childLevel) childLevel = tocLevels[i];
    }
    for (int i = openParent + 1; i < end; i++) {
      if (tocLevels[i] == childLevel) visibleRows.push_back(static_cast<int16_t>(i));
    }
  }
  // The window indexes rows, so it no longer describes the new list.
  windowStart = -1;
  windowCount = 0;
}

void EpubReaderChapterSelectionActivity::openPart(const int tocIndex) {
  openParent = tocIndex;
  openTitle = epub->getTocItem(tocIndex).title;
  buildVisibleRows();
  nav.selected = 0;
  nav.top = 0;
  requestUpdate();
}

bool EpubReaderChapterSelectionActivity::closePart() {
  if (!hierarchical || openParent < 0) return false;
  const int wasOpen = openParent;
  openParent = -1;
  openTitle.clear();
  buildVisibleRows();
  // Land back on the part that was open, not at the top of the book.
  nav.selected = 0;
  for (size_t row = 0; row < visibleRows.size(); row++) {
    if (visibleRows[row] == wasOpen) {
      nav.selected = static_cast<int>(row);
      break;
    }
  }
  nav.top = 0;
  requestUpdate();
  return true;
}

// Materialize the ListItem/label window starting at `start` (clamped). TOC
// entries are SD LUT reads (getTocItem), so this runs only when the viewport
// leaves the current window. Finishes with a batch prewarm of the window's
// CJK fallback glyphs -- one bounded SD pass per list page; repaints inside
// the window stay RAM-only.
void EpubReaderChapterSelectionActivity::refreshTocWindow(const int start) {
  const int total = listCount();
  int clamped = start;
  if (clamped > total - TOC_WINDOW) clamped = total - TOC_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = total - clamped < TOC_WINDOW ? total - clamped : TOC_WINDOW;
  for (int i = 0; i < windowCount; i++) {
    const auto tocItem = epub->getTocItem(tocIndexForRow(clamped + i));
    // Every row of a drill-down view sits at the same level, so the indent that
    // conveys depth in the flat list would only eat width here.
    std::string indent(!hierarchical && tocItem.level > 0 ? (tocItem.level - 1) * 2 : 0, ' ');
    windowLabels[i] = indent + tocItem.title;
    fui::ListItem item;
    item.label = windowLabels[i].c_str();
    item.actionValue = static_cast<int16_t>(clamped + i);
    windowItems[i] = item;
  }
  windowStart = clamped;

  struct PrewarmCtx {
    const std::string* labels;
    int count;
  } prewarmCtx{windowLabels, windowCount};
  renderer.prewarmFallbackText(
      uiScaleSpec().bodyFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < static_cast<uint32_t>(c->count) ? c->labels[i].c_str() : nullptr;
      },
      &prewarmCtx, static_cast<uint32_t>(windowCount));
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) {
    return;
  }
  // The activated row leaves this screen (finish); a lingering flash would gray
  // an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const int tocIndex = tocIndexForRow(index);
  if (tocIndex < 0) return;
  // The open part listed as its own first row is a destination, not a way back
  // into the list it already opened.
  if (hierarchical && tocIndex != openParent && hasChildren(tocIndex)) {
    openPart(tocIndex);
    return;
  }
  const auto tocItem = epub->getTocItem(tocIndex);
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

bool EpubReaderChapterSelectionActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // One level at a time: an open part closes back to the part list, and only
    // Back at the top leaves Contents. The global back-swipe arrives here too.
    if (closePart()) return true;
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (!epub) {
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band drawChrome paints the title in.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (!epub) {
    return;
  }
  if (listCount() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  // Materialize the row window for the final viewport (syncListViewport just
  // applied follow/clamping to nav.top) and hand list() the window with its
  // absolute base index.
  refreshTocWindow(nav.top);
  props.items = windowItems;
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  screen.list(props);
}

void EpubReaderChapterSelectionActivity::drawChrome() {
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false),
                 openParent >= 0 ? openTitle.c_str() : tr(STR_SELECT_CHAPTER));
}
