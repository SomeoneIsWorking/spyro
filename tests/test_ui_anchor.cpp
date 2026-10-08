// test_ui_anchor.cpp — the ONE horizontal anchoring policy, both answers and every refusal.
//
// The policy is pure arithmetic over an authored x and two frame widths, so every claim in
// docs/project-state.md S030 rests on this file: what 16:9 does to each class, what 4:3 does to
// each class (nothing), and what happens to an element the policy cannot honestly place.
#include "ui_anchor.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

using spyro::ui_anchor::Anchor;
using spyro::ui_anchor::Frame;
using spyro::ui_anchor::Refusal;

constexpr Frame k43{512, 512};
constexpr Frame k169{512, 684};

} // namespace

int main() {
  using spyro::ui_anchor::correction;
  using spyro::ui_anchor::margin;
  using spyro::ui_anchor::offset;
  using spyro::ui_anchor::place;

  // ── The frame arithmetic the policy is written in ─────────────────────────────────────────────
  expect(margin(k43) == 0, "4:3 has no margin: this is what makes 4:3 the identity everywhere");
  expect(margin(k169) == 86, "16:9 over 4:3 widens 512 -> 684 with an 86 px margin");

  // ── 4:3 IS THE IDENTITY, FOR EVERY CLASS, INCLUDING THE REFUSALS ──────────────────────────────
  for (const Anchor anchor : {Anchor::LeftEdge, Anchor::Centred, Anchor::RightEdge}) {
    const auto placed = place(anchor, 90, 28, k43);
    expect(placed.refusal == Refusal::None, "4:3 places a legal box");
    expect(placed.box.x == 90 && placed.box.width == 28 && placed.offset == 0,
           "4:3 leaves an authored element exactly where the guest put it");
  }

  // ── EACH CLASS'S ANSWER AT 16:9 ────────────────────────────────────────────────────────────────
  expect(offset(Anchor::LeftEdge, k169) == 0, "a left-edge element does not move");
  expect(offset(Anchor::Centred, k169) == 86, "a centred element moves by one margin");
  expect(offset(Anchor::RightEdge, k169) == 172, "a right-edge element moves by TWO margins");

  // The three properties the goal states, each as a distance that must be INVARIANT.
  {
    // Edge-left: the distance from the frame's left edge is the authored inset.
    const auto box = place(Anchor::LeftEdge, 40, 16, k169).box;
    expect(box.x == 40, "a left-edge element keeps its inset from the widened left edge");
    expect(box.x + box.width <= k169.drawn, "a left-edge element stays inside the widened frame");
  }
  {
    // Edge-right: the distance from the RIGHT edge is the authored one, 24 px here.
    const auto narrow = place(Anchor::RightEdge, 488 - 16, 16, k43).box;
    const auto wide = place(Anchor::RightEdge, 488 - 16, 16, k169).box;
    expect(narrow.x + narrow.width == 512 - 24, "the 4:3 right inset is 24 px");
    expect(wide.x + wide.width == k169.drawn - 24, "the 16:9 right inset is the same 24 px");
    // The negative that matters: a right-edge element placed as if it were centred would land at
    // 488 - 16 + 86, and its right inset would be 24 + 86 instead of 24.
    expect(wide.x != narrow.x + 86, "a right-edge element is not placed by the centring rule");
  }
  {
    // Centred: the distance from the frame's CENTRE is preserved, not the x.
    const int32_t narrowX = 186, narrowW = 100;
    const auto wide = place(Anchor::Centred, narrowX, narrowW, k169).box;
    const int32_t narrowOff = (narrowX + narrowW / 2) - k43.drawn / 2;
    const int32_t wideOff = (wide.x + wide.width / 2) - k169.drawn / 2;
    expect(narrowOff == -20, "the authored element really is 20 px left of the 4:3 centre");
    expect(wideOff == narrowOff, "a centred element keeps its offset from the centre");
  }
  {
    // Nothing stretches: every class keeps the AUTHORED width.
    for (const Anchor anchor : {Anchor::LeftEdge, Anchor::Centred, Anchor::RightEdge}) {
      expect(place(anchor, 100, 28, k169).box.width == 28,
             "an anchored element keeps its authored pixel width at 16:9");
    }
  }

  // ── THE CORRECTION, for an x that already carries the framework's centring ────────────────────
  expect(correction(Anchor::LeftEdge, k43) == 0, "the 4:3 correction is the identity");
  expect(correction(Anchor::Centred, k43) == 0,
         "a centred glyph is already where it belongs at 4:3");
  expect(*correction(Anchor::LeftEdge, k169) == -86,
         "a left-edge glyph is pulled back out of the centring the projection applied");
  expect(*correction(Anchor::Centred, k169) == 0, "a centred glyph is not moved a second time");
  expect(*correction(Anchor::RightEdge, k169) == 86,
         "a right-edge glyph is pushed out to the edge");
  expect(!correction(static_cast<Anchor>(9), k169).has_value(),
         "an unknown class has no correction");
  expect(!correction(Anchor::LeftEdge, Frame{512, 400}).has_value(),
         "a narrower frame has no correction");

  // The two halves of one HUD must agree: a sprite placed as LeftEdge and a glyph corrected as
  // LeftEdge move to the SAME x, or the element is torn apart by its own two producers.
  {
    const auto sprite = place(Anchor::LeftEdge, 90, 28, k169);
    const int32_t glyph =
        90 + 86 /* the projection's own centring */ + *correction(Anchor::LeftEdge, k169);
    expect(glyph == sprite.box.x, "the sprite and the glyph of one element agree at 16:9");
  }

  // ── EVERY REFUSAL, WITH ITS REASON
  // ──────────────────────────────────────────────────────────────
  expect(place(Anchor::LeftEdge, 90, 0, k169).refusal == Refusal::NonPositiveWidth,
         "a zero-width element is refused rather than placed");
  expect(place(Anchor::Centred, 90, -4, k169).refusal == Refusal::NonPositiveWidth,
         "a negative width is refused");
  expect(place(Anchor::RightEdge, 90, 8, Frame{512, 400}).refusal == Refusal::NarrowerFrame,
         "a frame NARROWER than the authored one is refused, not anchored");
  expect(place(Anchor::LeftEdge, 510, 8, k169).refusal == Refusal::OutsideAuthoredFrame,
         "a box that hangs off the authored frame is refused");
  expect(place(Anchor::Centred, -4, 8, k169).refusal == Refusal::OutsideAuthoredFrame,
         "a negative authored x is refused");
  expect(!offset(static_cast<Anchor>(9), k169).has_value(), "an unknown class has no offset");
  expect(place(static_cast<Anchor>(9), 90, 8, k169).refusal == Refusal::UnknownClass,
         "an unrecognised class is refused instead of defaulting to centred");
  expect(place(Anchor::LeftEdge, 0, 8, Frame{0, 684}).refusal == Refusal::NonPositiveWidth,
         "a zero authored width is refused");
  expect(place(Anchor::Centred, 0, 8, Frame{512, 0}).refusal == Refusal::NonPositiveWidth,
         "a zero drawn width is refused");
  // A refused element is left where the guest put it: `place` never returns a shifted box with a
  // refusal attached, so a producer that ignores the reason still draws the authored x.
  {
    const auto refused = place(Anchor::RightEdge, 600, 8, k169);
    expect(refused.refusal != Refusal::None, "an out-of-frame right-edge box is refused");
    expect(refused.offset == 0, "a refused element is not moved");
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d ui_anchor check(s) FAILED\n", failures);
    return 1;
  }
  std::printf("ui_anchor: 4:3 identity, three anchor classes, unchanged widths, every refusal\n");
  return 0;
}
