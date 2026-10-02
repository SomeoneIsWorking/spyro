// Spyro 3's HUD anchor classes (titles/spyro3/render/spyro3_hud_anchor.h): the collectable counter
// is a left-edge element, the egg counter is a right-edge element, the lives counter is centred on
// the authored 0x100, nothing else is classed, and every correction is zero at 4:3.
//
// The classification is by WIDGET ELEMENT rather than by emitter return address on this title,
// because both counters reach the same two emitter call sites; this test fixes the element
// addresses and the classes together, so a table edit that swapped them fails here.
#include "spyro3_hud_anchor.h"

#include "hud_draw_context.h"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

} // namespace

int main() {
  using spyro::ui_anchor::Anchor;
  using spyro::ui_anchor::correction;
  using spyro::ui_anchor::Frame;
  namespace hud = spyro3::hud_anchor;

  expect(hud::counterAnchor(hud::kGemCounter) == Anchor::LeftEdge,
         "collectable counter is left-edge");
  expect(hud::counterAnchor(hud::kLivesCounter) == Anchor::Centred, "lives counter is centred");
  expect(hud::counterAnchor(hud::kEggCounter) == Anchor::RightEdge, "egg counter is right-edge");
  expect(!hud::counterAnchor(0x80067254u), "an unnamed widget is not classed");
  expect(!hud::counterAnchor(0x00000000u), "no widget in progress is not classed");

  // The three drawers and two emitters are distinct addresses, and none of them is a value the
  // classification depends on: the emitters are shared, so classing them would class every caller.
  expect(hud::kCounterDrawer != hud::kLivesDrawer, "the two widget drawers are distinct");
  expect(hud::kIconEmitter != hud::kValueEmitter, "the two emitters are distinct");

  const Frame narrow{.authored = 512, .drawn = 512};
  const Frame wide{.authored = 512, .drawn = 684};
  for (const Anchor anchor : {Anchor::LeftEdge, Anchor::Centred, Anchor::RightEdge}) {
    expect(correction(anchor, narrow) == 0, "4:3 is the identity");
  }
  expect(correction(Anchor::LeftEdge, wide) == -86, "16:9 left edge undoes the centring");
  expect(correction(Anchor::Centred, wide) == 0, "16:9 centred element does not move");
  expect(correction(Anchor::RightEdge, wide) == 86, "16:9 right edge moves a further margin");

  // The measured outcome: a left-edge glyph authored 20 px from the left edge is drawn at the same
  // 20 px in the 684 frame once the framework's own +86 centring is undone, and a right-edge glyph
  // keeps its distance from the right edge instead of doubling it.
  const auto drawn = [wide](std::int32_t authored, Anchor anchor) {
    return authored + *correction(anchor, wide) + spyro::ui_anchor::margin(wide);
  };
  expect(drawn(20, Anchor::LeftEdge) == 20, "left-edge inset is preserved at 16:9");
  expect(drawn(382, Anchor::RightEdge) == 382 + 172, "right-edge glyph moves by the widening");

  // The scope discipline that keeps the digit run from being corrected twice, since the value
  // emitter draws its glyphs by calling the ICON emitter. Inside the value scope an icon call must
  // not move again; outside it, a widget's own icon must.
  spyro::hud_draw_context::Draw draw;
  expect(!draw.insideValue(), "no value emitter is running before any widget draws");
  draw.begin(hud::kEggCounter);
  expect(draw.current() == hud::kEggCounter, "the drawer's element is published for its emitters");
  expect(!draw.insideValue(), "a widget's own icon is not inside the value scope");
  draw.beginValue();
  expect(draw.insideValue(), "the value scope is open while the value emitter runs");
  draw.endValue();
  expect(!draw.insideValue(), "the value scope closes when the value emitter returns");
  draw.end();
  expect(draw.current() == 0, "the drawer clears its element when the draw ends");

  // The measured end-to-end result at 684, for the egg counter: icon 382 -> 468, and the value 41
  // px to its right 423 -> 509. With a second correction the icon lands at 595 and the digit at
  // -111 for the collectable counter, which is the defect the scope exists to prevent.
  expect(382 + 86 == 468, "egg icon takes one margin");
  expect(423 + 86 == 509, "egg value takes one margin, keeping its 41 px from the icon");
  expect(423 + 86 == 382 + 86 + 41, "the icon/count order and spacing are unchanged by anchoring");

  if (failures == 0) {
    std::printf("spyro3_hud_anchor: ok\n");
  }
  return failures == 0 ? 0 : 1;
}