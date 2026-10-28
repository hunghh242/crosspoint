#pragma once

#include <cstdint>
#include <vector>

// Outline navigation over a flattened TOC.
//
// The cache stores TOC entries in document order with a nesting level each, so
// an entry's children are the run of deeper entries that follows it. These
// helpers turn that into one level at a time: the roots, then one parent's
// direct children, so a collection of parts lists its parts rather than every
// chapter of every part at once.
//
// Rows are picked by parent relation rather than by "level equals the
// shallowest level present". Real TOCs are not tidy -- front matter often sits
// deeper than the parts that follow it, and some books jump from level 1
// straight to level 3 -- and filtering on the level alone silently makes those
// entries unreachable.
namespace toc_outline {

// No shallower entry precedes it, so it has no parent and belongs at the top.
inline bool isRoot(const std::vector<uint8_t>& levels, const int index) {
  for (int i = 0; i < index; i++) {
    if (levels[i] < levels[index]) return false;
  }
  return true;
}

// One past the last descendant of `index`.
inline int subtreeEnd(const std::vector<uint8_t>& levels, const int index) {
  const int total = static_cast<int>(levels.size());
  for (int i = index + 1; i < total; i++) {
    if (levels[i] <= levels[index]) return i;
  }
  return total;
}

inline bool hasChildren(const std::vector<uint8_t>& levels, const int index) {
  const int next = index + 1;
  return next < static_cast<int>(levels.size()) && levels[next] > levels[index];
}

// Nearest shallower entry before `index`, or -1 when it is a root.
inline int parentOf(const std::vector<uint8_t>& levels, const int index) {
  for (int i = index - 1; i >= 0; i--) {
    if (levels[i] < levels[index]) return i;
  }
  return -1;
}

// True when anything hangs off anything else, i.e. the outline is worth using.
inline bool isNested(const std::vector<uint8_t>& levels) {
  if (levels.empty()) return false;
  uint8_t shallowest = levels[0];
  for (const uint8_t l : levels) {
    if (l < shallowest) shallowest = l;
  }
  for (const uint8_t l : levels) {
    if (l > shallowest) return true;
  }
  return false;
}

// TOC indices to list for `openParent` (-1 = the roots). A parent that has a
// destination of its own leads its own child list: a part usually points at its
// title page, which would otherwise be unreachable because tapping the part
// opens its children instead of jumping.
inline void buildRows(const std::vector<uint8_t>& levels, const int openParent, const bool parentHasTarget,
                      std::vector<int16_t>& out) {
  out.clear();
  const int total = static_cast<int>(levels.size());
  if (openParent < 0) {
    for (int i = 0; i < total; i++) {
      if (isRoot(levels, i)) out.push_back(static_cast<int16_t>(i));
    }
    return;
  }
  if (parentHasTarget) out.push_back(static_cast<int16_t>(openParent));
  const int end = subtreeEnd(levels, openParent);
  // Direct children are the shallowest entries inside the subtree, which is not
  // always parent level + 1: a TOC that skips a level would list none.
  uint8_t childLevel = UINT8_MAX;
  for (int i = openParent + 1; i < end; i++) {
    if (levels[i] < childLevel) childLevel = levels[i];
  }
  for (int i = openParent + 1; i < end; i++) {
    if (levels[i] == childLevel) out.push_back(static_cast<int16_t>(i));
  }
}

}  // namespace toc_outline
