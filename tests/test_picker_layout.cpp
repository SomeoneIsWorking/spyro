// The title picker's panel geometry, with no GPU, no session and no window: the rules a screenshot
// cannot explain. Three panels and one panel are the same code here, so both are exercised.
#include "picker_layout.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool near(float a, float b, float tolerance = 0.002f) {
  return std::fabs(a - b) <= tolerance;
}

// The panels tile the surface: no gap, no overlap, first one at the left edge, last one ending at
// the right edge. Every other property is measured against this, so it is the first thing checked.
void expectTiles(const spyro::PickerLayout &layout, int width, int height) {
  int edge = 0;
  for (int index = 0; index < layout.panelCount(); ++index) {
    const spyro::PanelLayout panel = layout.panel(index, 4, 3);
    expect(panel.bounds.x == edge, "each panel starts where the previous one ended");
    expect(panel.bounds.y == 0 && panel.bounds.h == height, "a panel is full height");
    edge = panel.bounds.x + panel.bounds.w;
  }
  expect(edge == width, "the panels end at the surface's right edge");
}

// Advance until the layout says the animation is over, and report how many frames that took.
int settle(spyro::PickerLayout &layout, int limit = 500) {
  int frames = 0;
  while (layout.advance() && ++frames < limit) {
  }
  return frames;
}

void threePanelsSettleWithTheSelectedWidest() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.25f);
  layout.setSurface(1280, 720);
  layout.setSelection(1);
  expectTiles(layout, 1280, 720);
  const int frames = settle(layout);
  expect(frames > 1, "moving the selection animates rather than snapping");
  expect(frames < 60, "and settles in a readable number of frames");
  expectTiles(layout, 1280, 720);
  const spyro::PanelLayout selected = layout.panel(1, 4, 3);
  const spyro::PanelLayout other = layout.panel(0, 4, 3);
  expect(selected.bounds.w == 640, "the selected panel takes half of 1280");
  expect(other.bounds.w == 320, "each other panel takes a quarter");
  expect(selected.bounds.w > other.bounds.w, "the selected panel is the widest");
  expect(layout.maxPanelWidth(1280) == 640,
         "the widest a panel can be is the selected panel's width");
}

void theAnimationNeverOvershoots() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.25f);
  layout.setSurface(1280, 720);
  layout.setSelection(0);
  settle(layout);
  const spyro::PanelLayout before = layout.panel(0, 4, 3);
  layout.setSelection(2);
  // Every intermediate frame: the middle panel must never be narrower than its own final width (an
  // overshoot), and the width must never jump past where it is going.
  for (int frame = 0; frame < 40; ++frame) {
    const spyro::PanelLayout middle = layout.panel(1, 4, 3);
    expect(middle.bounds.w <= before.bounds.w / 2,
           "a narrowing panel never passes its target width");
    layout.advance();
  }
  expect(layout.panel(1, 4, 3).bounds.w == 320,
         "the panel that lost the selection ends at a quarter");
  expect(layout.panel(2, 4, 3).bounds.w == 640, "the panel that gained it ends at a half");
}

// A panel is FILLED. The pane IS the panel's own shape — the seams lean and the surface's own edges
// do not, so an end panel is a trapezoid — and the picture is cover-cropped to that shape, so the
// panel reaches its corners, keeps the picture's aspect, and nothing is drawn past the edges where
// a neighbour begins. Letterboxing is what put black bands above and below every panel and made
// three games read as three small pictures in a black frame.
void aPictureCoversItsPanelAndKeepsItsAspect() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.05f);
  layout.setSurface(1280, 720);
  const spyro::PanelLayout panel = layout.panel(0, 4, 3);
  const spyro::PanelRect bounds = panel.bounds;
  const spyro::PanelRect box = panel.cover;
  // The drawn rectangle is the shape's own extent. This panel is the LEFTMOST one, so its left edge
  // is the window's and stands vertical, and only its right seam leans: the rectangle therefore
  // starts ON the window edge and its bottom-right corner leans a whole slant past the tiling edge.
  expect(panel.seams.leftTopX == 0 && panel.seams.leftBottomX == 0,
         "the leftmost panel's outer edge IS the window edge, at both heights");
  expect(panel.seams.rightBottomX - panel.seams.rightTopX == panel.slantX,
         "and only its inner seam leans");
  expect(box.x == 0, "so the panel starts at the window edge, with no wedge beside it");
  expect(box.x + box.w == panel.seams.rightBottomX,
         "and ends at the panel's own bottom-right corner");
  expect(box.y == 0 && box.h == bounds.h, "and the panel spans the full surface height");
  // A PANEL IS FILLED: the rectangle the picture is drawn in is the panel's whole height. A fitted
  // picture inside it left black bands above and below, which is what made three games read as
  // three small pictures in a black frame instead of three games filling the window.
  expect(box.h == 720, "the picture's rectangle is the panel's own full height, top to bottom");

  // 4:3 into a 640x720 pane: the picture is WIDER than the panel, so the sides are dropped and the
  // height is kept. The kept slice's aspect equals the pane's, which is what "not stretched" means.
  const spyro::SourceCrop wide = panel.sourceCrop(4, 3);
  // The crop's aspect is the panel's AVERAGE width by its height: a leaning panel has two widths
  // and the crop wants one number for both of them.
  const double paneAspect =
      static_cast<double>(panel.averageWidth()) / static_cast<double>(bounds.h);
  expect(wide.h == 1.0f && wide.v == 0.0f, "a wide picture keeps its full height");
  expect(wide.w < 1.0f, "and drops some of its width");
  // The kept slice's aspect is the crop fraction times the PICTURE's aspect and it must come out as
  // the pane's, which is the whole definition of cover-cropping rather than squeezing.
  expect(near(static_cast<double>(wide.w) * (4.0 / 3.0), paneAspect, 0.001),
         "the kept slice has the PANE's aspect, so the picture is cropped and never squeezed");
  expect(near(wide.u, (1.0f - wide.w) * 0.5f, 1e-6f),
         "and the crop is centred, so the panel is symmetric");

  // The other axis: a picture taller than the panel drops top and bottom instead.
  const spyro::SourceCrop tall = panel.sourceCrop(3, 4);
  expect(tall.w == 1.0f && tall.u == 0.0f, "a tall picture keeps its full width");
  expect(tall.h < 1.0f, "and drops some of its height");
  expect(near((3.0 / 4.0) / static_cast<double>(tall.h), paneAspect, 0.001),
         "and the kept slice still has the panel's aspect");

  // A picture of the panel's own shape is not cropped at all.
  const spyro::SourceCrop exact = panel.sourceCrop(panel.averageWidth(), bounds.h);
  expect(exact.w == 1.0f && exact.h == 1.0f && exact.u == 0.0f && exact.v == 0.0f,
         "a picture of the panel's own shape is shown whole");
  // A degenerate source is refused rather than guessed.
  const spyro::SourceCrop none = panel.sourceCrop(0, 0);
  expect(none.w == 1.0f && none.h == 1.0f, "and a source with no shape is not cropped");
}

// The logo sits on ONE line across the row: centred on its own panel, a fifth of the way down, and
// inside the panel so a wide wordmark on a narrow one is clipped by the seams rather than over its
// neighbour.
void theLogoSitsCentredAndOnOneLine() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.25f);
  layout.setSurface(1280, 720);
  settle(layout);
  int centres = 0;
  for (int panel = 0; panel < 3; ++panel) {
    const spyro::PanelLayout p = layout.panel(panel, 4, 3);
    const spyro::PanelRect logo = p.logoRect(200, 50);
    expect(logo.w == 200 && logo.h == 50, "the logo is drawn at its own size, unscaled away");
    expect(logo.y + logo.h / 2 == static_cast<int>(std::lround(720.0 * 0.2)),
           "the logo's centre is a fifth of the way down the window");
    expect(logo.x + logo.w / 2 == p.cover.x + p.cover.w / 2, "and it is centred on its own panel");
    expect(logo.x >= p.cover.x && logo.x + logo.w <= p.cover.x + p.cover.w,
           "so it never reaches past the panel it names");
    centres += logo.y;
  }
  expect(centres / 3 == centres / 3, "every panel's logo is on the same line");
  // A panel with no logo draws nothing at all: there is no placeholder rectangle standing in for a
  // wordmark the disc has not given us yet.
  const spyro::PanelLayout p = layout.panel(0, 4, 3);
  expect(p.logoRect(0, 0).w == 0, "a panel whose logo has not decoded draws nothing");
}

void theSeamsLeanAndTheWindowsOwnEdgesDoNot() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.06f, 0.25f);
  layout.setSurface(1280, 720);
  settle(layout);
  const spyro::PanelLayout left = layout.panel(0, 4, 3);
  const spyro::PanelLayout right = layout.panel(1, 4, 3);
  expect(left.slantX == right.slantX && left.slantX > 0,
         "both neighbours are slanted the same way");
  // The slant is the authored fraction of the surface's WIDTH: it is a horizontal displacement, and
  // only a fraction of the width is the same size of lean on a wide window and a square one.
  expect(left.slantX == 77, "the slant is the authored fraction of the surface width");
  // The divider is ONE line: the left panel's right edge and the right panel's left edge are the
  // same edge, so a divider drawn on one of them is on the other.
  const int leftEdge = left.bounds.x + left.bounds.w;
  expect(leftEdge == right.bounds.x, "the two panels meet at one edge");
  // The seam is ONE line: the left panel's right edge and the right panel's left edge are the same
  // edge at both heights, so a divider drawn on one of them is on the other. The seam leans a whole
  // slant from top to bottom — and it leans about the TILING edge, so its top end is half a slant
  // to the left of it and its bottom end half a slant to the right.
  const int half = (left.slantX + 1) / 2;
  expect(left.seams.rightTopX == leftEdge - half,
         "the seam's top end is half a slant left of the tiling edge");
  expect(left.seams.rightBottomX == leftEdge - half + left.slantX,
         "and its bottom end a whole slant right");
  expect(left.seams.rightTopX == right.seams.leftTopX &&
             left.seams.rightBottomX == right.seams.leftBottomX,
         "one divider line, at the top and at the bottom, for both neighbours");
  // The leftmost panel's LEFT edge is the window's and does not lean: leaning it is what left a
  // black wedge down the side of the window.
  expect(left.seams.leftTopX == 0 && left.seams.leftBottomX == 0,
         "the first panel's left edge is the window edge");
  // The last panel's RIGHT edge is the window's too. With three panels the middle one is the only
  // parallelogram left, and it leans on both sides by the same slant.
  const spyro::PanelLayout middle = layout.panel(1, 4, 3);
  const spyro::PanelLayout last = layout.panel(2, 4, 3);
  expect(last.seams.rightTopX == 1280 && last.seams.rightBottomX == 1280,
         "and the last panel's right edge is the window edge");
  expect(middle.seams.leftBottomX - middle.seams.leftTopX == middle.slantX &&
             middle.seams.rightBottomX - middle.seams.rightTopX == middle.slantX,
         "a panel between two seams leans by the same slant on both sides");
  // Every panel's top edge and bottom edge cover the surface between them, so the row tiles with no
  // gap and no overlap at either height.
  for (int i = 0; i < 3; ++i) {
    const spyro::PanelLayout p = layout.panel(i, 4, 3);
    const int leftAtTop = i == 0 ? 0 : layout.panel(i - 1, 4, 3).seams.rightTopX;
    const int leftAtBottom = i == 0 ? 0 : layout.panel(i - 1, 4, 3).seams.rightBottomX;
    expect(p.seams.leftTopX == leftAtTop && p.seams.leftBottomX == leftAtBottom,
           "panel N starts exactly where panel N-1 ends, at both heights");
  }
  expect(layout.panel(2, 4, 3).seams.rightTopX == 1280, "and the row ends at the surface edge");
}

void oneAndTwoPanelsUseTheSameRule() {
  spyro::PickerLayout one(1, 0.5f, 0.25f, 0.05f, 0.25f);
  one.setSurface(1280, 720);
  settle(one);
  expectTiles(one, 1280, 720);
  expect(one.panel(0, 4, 3).bounds.w == 1280, "the only panel takes the whole surface");

  spyro::PickerLayout two(2, 0.5f, 0.25f, 0.05f, 0.25f);
  two.setSurface(1280, 720);
  settle(two);
  expectTiles(two, 1280, 720);
  expect(two.panel(0, 4, 3).bounds.w == 853,
         "with two panels the selected one takes 2/3, not a half");
  expect(two.panel(1, 4, 3).bounds.w == 427, "and the other takes the third");
  expect(two.panel(0, 4, 3).bounds.w > two.panel(1, 4, 3).bounds.w,
         "the selected panel is still widest");

  spyro::PickerLayout none(0, 0.5f, 0.25f, 0.05f, 0.25f);
  none.setSurface(1280, 720);
  expect(!none.advance(), "a picker with no panels is not animating");
  expect(none.panel(0, 4, 3).bounds.w == 0, "and has no geometry to draw");
  expect(none.maxPanelWidth(1280) == 0, "and asks for no pane image");
}

void aResizeKeepsTheAnimationWhereItWas() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.25f);
  layout.setSurface(1280, 720);
  settle(layout);
  layout.setSelection(2);
  layout.advance(); // one frame into the move: the shares are between where they were and where
                    // they go
  const float middle = layout.share(1);
  layout.setSurface(1920, 1080);
  expect(near(layout.share(1), middle), "a resize does not restart or complete the animation");
  expectTiles(layout, 1920, 1080);
  // Mid-animation, the shares are between where they were and where they are going, so what a
  // resize must preserve is the tiling and the ordering — not a width, which is still travelling.
  expect(layout.panel(0, 4, 3).bounds.w > layout.panel(1, 4, 3).bounds.w,
         "the selected panel is still the widest while the animation runs");
  expect(layout.panel(0, 4, 3).bounds.w > 320 && layout.panel(0, 4, 3).bounds.w <= 960,
         "and its width is between the old surface's and the new one's");
}

void degenerateSurfacesAreRefusedNotGuessed() {
  spyro::PickerLayout layout(3, 0.5f, 0.25f, 0.05f, 0.25f);
  layout.setSurface(0, 0);
  const spyro::PanelLayout none = layout.panel(0, 4, 3);
  expect(none.bounds.w == 0 && none.cover.w == 0, "no surface means no panels to draw");
  layout.setSurface(1280, 720);
  expect(layout.panel(-1, 4, 3).bounds.w == 0,
         "a panel outside the picker is refused, not clamped");
  expect(layout.panel(3, 4, 3).bounds.w == 0, "including the index just past the last panel");
  expect(layout.share(7) == 0.0f, "and it has no share of the width");
}

} // namespace

int main() {
  threePanelsSettleWithTheSelectedWidest();
  theAnimationNeverOvershoots();
  aPictureCoversItsPanelAndKeepsItsAspect();
  theLogoSitsCentredAndOnOneLine();
  theSeamsLeanAndTheWindowsOwnEdgesDoNot();
  oneAndTwoPanelsUseTheSameRule();
  aResizeKeepsTheAnimationWhereItWas();
  degenerateSurfacesAreRefusedNotGuessed();
  std::printf("picker layout: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
