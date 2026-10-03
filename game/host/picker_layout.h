// picker_layout.h — the picker's PANEL GEOMETRY: where each panel is, how wide it is this frame,
// where the game's picture goes inside it, and how the slanted dividers are cut.
//
// WHY IT IS ITS OWN OWNER, AND WHY IT IS PURE. This is the only part of the picker that can be
// wrong in a way a screenshot cannot explain: three panels that overlap, a picture stretched
// instead of fitted, a divider that is one line for the left panel and a different line for its
// neighbour. None of that needs a GPU, a session, a disc or a clock to decide, so none of it is
// decided where those live — tests/test_picker_layout.cpp drives every rule below with numbers.
//
// THE SHAPE, IN ONE PARAGRAPH. The panels tile the surface edge to edge and span its FULL height;
// the selected panel takes `selectedShare` of the width and every other panel `unselectedShare`,
// and both ANIMATE, because a picker whose panels jump is a list with pictures in it. The SEAMS
// lean: a boundary between two panels is a line whose bottom end is `slantX` pixels right of its
// top end, cut once and drawn once, so panel i's right edge and panel i+1's left edge are the same
// line. The surface's OWN edges do not lean — the leftmost panel's left edge and the rightmost
// panel's right edge are the window's, and stand vertical.
//
// A SEAM IS A MASK, NOT A SHEAR. Each panel reports its shape as two boundary LINES (a column at
// the top of the panel and a column at the bottom of it, for the left boundary and the right), and
// the compositor draws an upright rectangle and drops whatever falls outside them. Nothing about
// the picture is sheared: the guest's geometry inside a panel is exactly the guest's geometry,
// upright, which is the whole reason the seams are numbers in a uniform instead of corners in a
// quad.
//
// THE PICTURE COVERS ITS PANEL. The panels tile the surface edge to edge and span its full height,
// so a panel IS the window's height and a quarter or half of its width. The picture is scaled to
// that height and its WIDTH is cropped — the panel's own shape, not a letterboxed picture inside a
// black frame: three games have to read as three games filling the window, and a black band above
// and below each of them is what stops that.
#pragma once

#include <cmath>
#include <vector>

namespace spyro {

// A panel's logo sits with its centre a fifth of the way down the panel, so every panel's wordmark
// is on one line across the row however wide its panel is.
inline constexpr double logoCentrePerHeight = 0.2;

// The most panels one layout can lay out. A bound, not a policy: the picker shows one panel per
// AVAILABLE catalog title, and a catalog longer than this is refused by construction rather than by
// a panel silently landing at zero width.
inline constexpr int kMaxPickerPanels = 8;

// An integer rectangle in surface pixels, origin top-left. The picker's own geometry type: the
// panel layout is computed in whole pixels because that is what the compositor and the window
// consume.
struct PanelRect {
  int x = 0, y = 0, w = 0, h = 0;
};

// Which part of a panel's picture is shown, as fractions of that picture: (0,0,1,1) is all of it.
// The panel COVER-CROPS: it is a different shape from the picture, so the overflow is dropped
// rather than letterboxed, and this is what says how much.
struct SourceCrop {
  float u = 0.0f, v = 0.0f, w = 1.0f, h = 1.0f;
};

// One panel's geometry for one frame.
struct PanelLayout {
  // The panel's own slice of the surface: full height, between the neighbouring boundaries. This is
  // the TILING rect: the panel's own share of the width, with both of its seams cut through it.
  PanelRect bounds;
  // THE SEAMS, in surface pixels: each boundary as the column it sits in at y = 0 and at
  // y = the surface's height.
  //
  // These are MASK LINES and not corners. A panel's picture is drawn as an upright rectangle and
  // these two lines decide which parts of it survive, so a guest's geometry inside a panel is never
  // sheared by a seam that is only decoration. A panel is not a trapezoid any more; the two numbers
  // per boundary are all that is left of the shape, and they are exactly what two neighbours need
  // to agree on to cut one line between them.
  //
  // Only the seams BETWEEN panels lean. The leftmost panel's left boundary and the rightmost
  // panel's right boundary are the window's own edges, so their two columns are equal and stand
  // vertical: leaning those put a black wedge down each side of the window.
  struct Seams {
    int leftTopX = 0, rightTopX = 0;       // at y = 0
    int leftBottomX = 0, rightBottomX = 0; // at y = the surface's height
  };
  Seams seams;
  // The panel's AVERAGE width across its height — half the top boundary's span, half the bottom
  // one's. A fit wants one width for a shape that has two, and a panel's own two differ by at most
  // one slant.
  int averageWidth() const {
    return ((seams.rightTopX - seams.leftTopX) + (seams.rightBottomX - seams.leftBottomX)) / 2;
  }
  // The rectangle the pane is DRAWN: upright, covering every point the seams enclose, top to the
  // surface's top edge and bottom to its bottom edge. It is wider than the panel by up to one
  // slant, because the two neighbours' rectangles overlap across the shared seam and the mask
  // decides which of them owns each pixel there. The picture IS this rectangle: a panel is filled,
  // not letterboxed.
  PanelRect cover;
  // WHICH PART OF THE PICTURE this panel shows, so that the picture fills `cover` with its aspect
  // intact: scaled to the panel's height and cropped on the other axis. A degenerate source is
  // shown whole rather than guessed at.
  SourceCrop sourceCrop(int pictureWidth, int pictureHeight) const {
    if (pictureWidth <= 0 || pictureHeight <= 0 || cover.h <= 0 || averageWidth() <= 0) {
      return SourceCrop{};
    }
    const double panelAspect = static_cast<double>(averageWidth()) / static_cast<double>(cover.h);
    const double pictureAspect =
        static_cast<double>(pictureWidth) / static_cast<double>(pictureHeight);
    if (pictureAspect > panelAspect) {
      // A picture WIDER than the panel: the height is the panel's, so the sides are what is lost.
      const double keep = panelAspect / pictureAspect;
      return SourceCrop{
          static_cast<float>((1.0 - keep) * 0.5), 0.0f, static_cast<float>(keep), 1.0f};
    }
    const double keep = pictureAspect / panelAspect;
    return SourceCrop{0.0f, static_cast<float>((1.0 - keep) * 0.5), 1.0f, static_cast<float>(keep)};
  }
  // WHERE THE LOGO GOES. Centred horizontally on the panel, with its own vertical centre at
  // `logoCentrePerHeight` of the way down the panel — one line across the row, whatever width any
  // panel has. A fraction of the panel's own height rather than a pixel offset, so the composition
  // is the same at any window size.
  PanelRect logoRect(int logoWidth, int logoHeight) const {
    if (logoWidth <= 0 || logoHeight <= 0 || cover.h <= 0) {
      return PanelRect{cover.x, cover.y, 0, 0};
    }
    const int centreY =
        static_cast<int>(std::lround(static_cast<double>(cover.h) * logoCentrePerHeight));
    return PanelRect{
        cover.x + (cover.w - logoWidth) / 2, centreY - logoHeight / 2, logoWidth, logoHeight};
  }
  int slantX = 0; // the top of a seam sits this many pixels LEFT of its bottom
};

// The animated panel layout of the picker.
//
// Construction takes the panel COUNT, not a list of titles: a panel is geometry, and what will be
// shown in it is a session's business. One panel and two panels are the same code as three — there
// is no "three panels wide" case anywhere below.
class PickerLayout {
public:
  // `responsePerFrame` is the fraction of the remaining distance each panel's share closes per
  // frame, so 0.25 settles in a dozen frames and 1.0 would snap. It is clamped away from 0 and 1
  // because those two are not animations: 0 never moves, 1 overshoots nothing and looks like a
  // jump.
  PickerLayout(int panelCount,
               float selectedShare,
               float unselectedShare,
               float slantPerWidth,
               float responsePerFrame);

  // The panel that is the wide one. Every other panel narrows towards its own share on the next
  // advance(); the selection itself never moves a panel's edge by itself.
  void setSelection(int selected);
  int selection() const {
    return mSelected;
  }

  // The surface the panels tile, in pixels. A resize re-derives every rectangle from the CURRENT
  // shares, so a resized window keeps the animation where it was instead of restarting it.
  void setSurface(int width, int height);

  // One picker frame of the width animation. Returns true while any panel is still settling, which
  // is the only question a caller has about the animation: is it over?
  bool advance();

  int panelCount() const {
    return mPanelCount;
  }
  // The panel's geometry for the frame. `pictureWidth`/`pictureHeight` are the ASPECT of what will
  // be shown in it (1x1 for a square source); a non-positive source covers the panel's own shape.
  PanelLayout panel(int index, int pictureWidth, int pictureHeight) const;

  // The panel's current share of the width. Exposed so a diagnostic can say what the animation is
  // doing; the layout itself never reads it back.
  float share(int index) const;

  // The WIDEST any panel can be at this surface width: the selected panel's own share once the
  // animation has arrived. A host that composites panel pictures needs one image size for every
  // session, and resizing it every frame of the animation would rebuild every session's picture for
  // a width that is about to change anyway.
  int maxPanelWidth(int surfaceWidth) const;

  // The most panels one layout can lay out. A caller with more available titles than this must say
  // so rather than hand over a count that would be silently truncated.
  static constexpr int maxPanels() {
    return kMaxPickerPanels;
  }

private:
  // Re-point every panel at the share its selection calls for. Called by the constructor and by
  // every selection change; advance() is what walks each panel there.
  void retarget();
  int mPanelCount;
  float mSelectedShare;
  float mUnselectedShare;
  float mSlantPerWidth;
  float mResponse;
  int mSelected = 0;
  int mSurfaceW = 0, mSurfaceH = 0;
  // The current share of the width per panel; `mTargetShare` is where each one is heading. Kept
  // apart because the animation IS the difference between them, and collapsing them would make
  // advance() a no-op with extra steps.
  std::vector<float> mShare;
  std::vector<float> mTargetShare;
};

} // namespace spyro
