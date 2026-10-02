// Spyro 2's HUD anchor classes (titles/spyro2/render/spyro2_hud_anchor.h): the gem counter is a
// left-edge element, the orb counter and the meter are right-edge elements, nothing else is
// classed, and every correction is zero at 4:3.
#include "spyro2_hud_anchor.h"

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
  namespace hud = spyro2::hud_anchor;

  expect(hud::counterAnchor(hud::kGemCounter) == Anchor::LeftEdge, "gem counter is left-edge");
  expect(hud::counterAnchor(hud::kOrbCounter) == Anchor::RightEdge, "orb counter is right-edge");
  expect(!hud::counterAnchor(0x8006766Cu), "the lives element is not a counter");
  for (const std::uint32_t meterReturn : hud::kMeterEmitterReturns) {
    expect(hud::emitterAnchor(meterReturn) == Anchor::RightEdge,
           "meter emitter call is right-edge");
  }
  expect(!hud::emitterAnchor(0x80052C44u), "a lives emitter call is not classed");
  expect(!hud::emitterAnchor(0x80052E8Cu), "a jal site is not its return address");

  const Frame narrow{.authored = 512, .drawn = 512};
  const Frame wide{.authored = 512, .drawn = 684};
  for (const Anchor anchor : {Anchor::LeftEdge, Anchor::Centred, Anchor::RightEdge}) {
    expect(correction(anchor, narrow) == 0, "4:3 is the identity");
  }
  expect(correction(Anchor::LeftEdge, wide) == -86, "16:9 left edge undoes the centring");
  expect(correction(Anchor::RightEdge, wide) == 86, "16:9 right edge moves a further margin");

  if (failures == 0) {
    std::printf("spyro2_hud_anchor: ok\n");
  }
  return failures == 0 ? 0 : 1;
}
