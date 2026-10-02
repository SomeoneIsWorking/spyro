// The shared terrain screen window (game/render/guest_terrain_screen.h): with no margin the three
// horizontal tests are the image's three integer shapes over every packed SXY word tested, and a
// margin widens the window symmetrically without ever narrowing it. Both titles' authored width is
// asserted here too, because the window is now stated per title rather than compiled in.
#include "guest_terrain_screen.h"

#include "spyro2_render_facts.h"
#include "spyro3_render_facts.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

std::uint32_t sxy(std::int32_t x, std::int32_t y) {
  return (static_cast<std::uint32_t>(x) & 0xFFFFu) + (static_cast<std::uint32_t>(y) << 16);
}

} // namespace

int main() {
  using spyro::guest_terrain::ScreenBounds;

  // Retail's spellings, over x and y wide enough to wrap the shifted comparisons.
  const ScreenBounds retail(spyro2::kTerrainFacts.nativeWidth, 0);
  bool agrees = true;
  for (std::int32_t x = -1100; x <= 1100; x += 3) {
    for (std::int32_t y = -300; y <= 300; y += 50) {
      const std::uint32_t word = sxy(x, y);
      agrees = agrees && retail.outsideHorizontal(word) == ((word & 0xFE00u) != 0);
      agrees =
          agrees && retail.atOrLeftOfLeft(word) == (static_cast<std::int32_t>(word << 16) <= 0);
      agrees = agrees && retail.atOrRightOfRight(word) ==
                             (static_cast<std::int32_t>((word << 16) - 0x2000000u) >= 0);
    }
  }
  expect(agrees, "no margin reproduces the image's three tests");

  // BOTH TITLES state the same authored window, which is what lets one implementation of the rule
  // serve the two images; if either declared a different one, the margin above would be wrong for
  // it.
  expect(spyro2::kTerrainFacts.nativeWidth == 512 && spyro3::kTerrainFacts.nativeWidth == 512,
         "both titles author a 512-column window");
  // And both name their own pass tables: a table a title inherited from the other image would read
  // the wrong sector data, which no capture at 4:3 would reveal.
  expect(spyro2::kTerrainFacts.entry != spyro3::kTerrainFacts.entry,
         "the two titles bind different terrain drawers");
  expect(spyro2::kTerrainFacts.sectorVisibility != spyro3::kTerrainFacts.sectorVisibility,
         "the two titles call different sector-visibility leaves");
  expect(spyro2::kTerrainFacts.coarse.triangleCracks != spyro3::kTerrainFacts.coarse.triangleCracks,
         "the two titles read different split tables");

  // A 16:9 margin of 86 columns: [-86, 598).
  const ScreenBounds wide(spyro2::kTerrainFacts.nativeWidth, 86);
  expect(wide.left() == -86 && wide.right() == 598, "16:9 window");
  expect(!wide.outsideHorizontal(sxy(-86, 40)), "left edge inside");
  expect(wide.outsideHorizontal(sxy(-87, 40)), "past left edge outside");
  expect(!wide.outsideHorizontal(sxy(597, 40)), "right edge inside");
  expect(wide.outsideHorizontal(sxy(598, 40)), "past right edge outside");
  expect(wide.atOrLeftOfLeft(sxy(-86, 40)) && !wide.atOrLeftOfLeft(sxy(-85, 40)), "at or left");
  expect(wide.atOrRightOfRight(sxy(598, 40)) && !wide.atOrRightOfRight(sxy(597, 40)),
         "at or right");
  expect(!wide.atOrLeftOfLeft(sxy(0, 40)) && !wide.atOrRightOfRight(sxy(512, 40)),
         "retail's edges are inside the wide window");

  // A negative margin never narrows the window.
  const ScreenBounds narrow(spyro2::kTerrainFacts.nativeWidth, -20);
  expect(narrow.left() == 0 && narrow.right() == spyro2::kTerrainFacts.nativeWidth,
         "never narrower");

  if (failures != 0) {
    return 1;
  }
  std::puts("guest_terrain_screen: ok");
  return 0;
}
