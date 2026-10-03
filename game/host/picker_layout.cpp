#include "picker_layout.h"

#include <algorithm>
#include <cmath>

namespace spyro {
namespace {

// Sub-pixel slack: below this a panel's share has arrived. A float compared with == would either
// never settle (0.1 * 0.75 forever) or settle instantly; the animation's last frame is the one that
// says "close enough", and this is what it says it with.
constexpr float kSettled = 0.0005f;

} // namespace

PickerLayout::PickerLayout(int panelCount,
                           float selectedShare,
                           float unselectedShare,
                           float slantPerWidth,
                           float responsePerFrame)
    : mPanelCount(std::clamp(panelCount, 0, kMaxPickerPanels)), mSelectedShare(selectedShare),
      mUnselectedShare(unselectedShare), mSlantPerWidth(slantPerWidth),
      mResponse(std::clamp(responsePerFrame, 0.01f, 1.0f)) {
  retarget();
  mShare = mTargetShare; // a layout starts settled; the first advance() has nothing to animate
}

void PickerLayout::setSelection(int selected) {
  if (mPanelCount <= 0) {
    return;
  }
  mSelected = std::clamp(selected, 0, mPanelCount - 1);
  retarget();
}

void PickerLayout::retarget() {
  mTargetShare.assign(static_cast<std::size_t>(mPanelCount), mUnselectedShare);
  if (mPanelCount <= 0) {
    return;
  }
  // The shares are RELATIVE and normalised by their own total, so the panels always tile the
  // surface whatever the requested numbers are: a picker with two panels and one with three both
  // fill the width, and a share of 0.5 is a share of the width rather than of a fixed count.
  mTargetShare[static_cast<std::size_t>(mSelected)] = mSelectedShare;
}

void PickerLayout::setSurface(int width, int height) {
  mSurfaceW = std::max(width, 0);
  mSurfaceH = std::max(height, 0);
}

bool PickerLayout::advance() {
  if (mPanelCount <= 0) {
    return false;
  }
  bool settling = false;
  for (int i = 0; i < mPanelCount; ++i) {
    const std::size_t index = static_cast<std::size_t>(i);
    const float remaining = mTargetShare[index] - mShare[index];
    if (std::fabs(remaining) <= kSettled) {
      mShare[index] = mTargetShare[index];
      continue;
    }
    // Move a fixed FRACTION of what is left, so the last part of the travel takes the same time per
    // pixel as the first: a fixed step per frame would make a long travel snap and a short one
    // crawl.
    mShare[index] += remaining * mResponse;
    settling = true;
  }
  return settling;
}

float PickerLayout::share(int index) const {
  if (index < 0 || index >= mPanelCount) {
    return 0.0f;
  }
  return mShare[static_cast<std::size_t>(index)];
}

int PickerLayout::maxPanelWidth(int surfaceWidth) const {
  if (mPanelCount <= 0 || surfaceWidth <= 0) {
    return 0;
  }
  float total = 0.0f;
  for (float value : mTargetShare) {
    total += value;
  }
  if (total <= 0.0f) {
    return 0;
  }
  return std::max(1,
                  static_cast<int>(std::lround(
                      surfaceWidth * mTargetShare[static_cast<std::size_t>(mSelected)] / total)));
}

PanelLayout PickerLayout::panel(int index, int pictureWidth, int pictureHeight) const {
  PanelLayout out;
  if (mPanelCount <= 0 || index < 0 || index >= mPanelCount || mSurfaceW <= 0 || mSurfaceH <= 0) {
    return out;
  }
  float total = 0.0f;
  for (int i = 0; i < mPanelCount; ++i) {
    total += mShare[static_cast<std::size_t>(i)];
  }
  if (total <= 0.0f) {
    return out;
  }
  // Panels tile edge to edge: each one starts where the one before it ended, so a panel's right
  // edge IS its neighbour's left edge by construction rather than by two rects agreeing to.
  float edge = 0.0f;
  for (int i = 0; i < index; ++i) {
    edge += mShare[static_cast<std::size_t>(i)] / total;
  }
  const float start = edge;
  const float width = mShare[static_cast<std::size_t>(index)] / total;
  const int x = static_cast<int>(std::lround(start * mSurfaceW));
  const int right = static_cast<int>(std::lround((start + width) * mSurfaceW));
  out.bounds = PanelRect{x, 0, std::max(right - x, 0), mSurfaceH};
  // THE SLANT IS A FRACTION OF THE SURFACE'S WIDTH, not its height. It is a horizontal
  // displacement, and a fraction of the width is the only reading that stays the same proportion of
  // a panel on a wide window and a square one; as a fraction of the height a 16:9 window got a lean
  // of a few dozen pixels, which is not visibly a slant at all. Nine percent is enough that the
  // dividers read as slanted lines from across a room and small enough that the panels still tile
  // the surface.
  out.slantX = static_cast<int>(std::lround(mSlantPerWidth * mSurfaceW));
  // THE SEAMS LEAN; THE SURFACE'S OWN EDGES DO NOT. The tiling edge `right` is where this panel
  // ends and the next begins, and that boundary is a line leaning `slantX` to the right as it goes
  // down — so at the top of the surface the seam is half a slant LEFT of the tiling edge and at the
  // bottom half a slant RIGHT of it. A panel's left edge is its LEFT neighbour's seam and its right
  // edge is its RIGHT neighbour's seam, except at the two ends of the row, where the edge is the
  // window's own and stands vertical: leaning those left a black wedge down each side of the
  // window, because the panel's own corner fell outside the surface it is supposed to fill.
  const int half = (out.slantX + 1) / 2;
  const bool first = index == 0;
  const bool last = index + 1 == mPanelCount;
  out.seams.leftTopX = first ? 0 : x - half;
  out.seams.rightTopX = last ? mSurfaceW : right - half;
  out.seams.leftBottomX = first ? 0 : x - half + out.slantX;
  out.seams.rightBottomX = last ? mSurfaceW : right - half + out.slantX;
  // `cover` is the UPRIGHT rectangle the pane is drawn in: it has to enclose every point the two
  // seams pass through, so it spans from the leftmost to the rightmost of them. Two neighbours
  // overlap across the seam they share by up to one slant, and the mask is what makes each pixel
  // belong to exactly one of them.
  const int coverLeft = std::min(out.seams.leftTopX, out.seams.leftBottomX);
  const int coverRight = std::max(out.seams.rightTopX, out.seams.rightBottomX);
  out.cover = PanelRect{coverLeft, 0, std::max(coverRight - coverLeft, 0), mSurfaceH};
  // The picture is this rectangle, filled: the panel's own shape decides what is cropped from the
  // source, through `sourceCrop` above. There is nothing else in a panel to leave a band for.
  return out;
}

} // namespace spyro
