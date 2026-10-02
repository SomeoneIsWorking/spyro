// The shared moby visibility frustum (game/render/guest_moby_frustum.h): the 4:3 weights
// are retail's literal (4, 3) and reproduce the image's plane test, the widened weights put the
// horizontal half-angle at the plan's projection width, and a plan that does not widen never
// narrows.
#include "guest_moby_frustum.h"

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

// The image's own horizontal reject, 80043A68..80043A8C, in 32-bit arithmetic.
bool retailOutsideHorizontal(std::int32_t x, std::int32_t z, std::uint32_t radius) {
  const auto margins = spyro::guest_moby_frustum::sphereMargins(radius);
  const std::int32_t lateral = (x < 0 ? -x : x) - margins.horizontalLateral;
  const std::int32_t depth = z + margins.horizontalDepth;
  return lateral * 4 - depth * 3 >= 0;
}

} // namespace

int main() {
  using spyro::guest_moby_frustum::behindEye;
  using spyro::guest_moby_frustum::insideFrustum;
  using spyro::guest_moby_frustum::kRetailSlope;
  using spyro::guest_moby_frustum::outsideHorizontal;
  using spyro::guest_moby_frustum::sphereMargins;
  using spyro::guest_moby_frustum::ViewPoint;
  using spyro::guest_moby_frustum::widenedSlope;

  // Retail's shift sums for a radius of 0x100 (a mesh radius byte of 16).
  const auto margins = sphereMargins(0x100);
  expect(margins.horizontalLateral == 0x80 + 0x40 + 0x08, "horizontal lateral margin");
  expect(margins.horizontalDepth == 0x80 + 0x10 + 0x08, "horizontal depth margin");
  expect(margins.verticalLateral == 0x80 + 0x10, "vertical lateral margin");
  expect(margins.verticalDepth == 0x40 + 0x10, "vertical depth margin");

  // At 4:3 the shared test is the image's test, on both sides of the plane and of the axis.
  bool agrees = true;
  for (std::int32_t x = -3000; x <= 3000; x += 37) {
    for (std::int32_t z = -500; z <= 4000; z += 41) {
      agrees = agrees && outsideHorizontal(ViewPoint{x, 0, z}, margins, kRetailSlope) ==
                             retailOutsideHorizontal(x, z, 0x100);
    }
  }
  expect(agrees, "the 4:3 slope reproduces the image's horizontal reject");

  // The widened slope: 512 -> 684 at 16:9 is tan 3/4 * 684/512.
  expect(widenedSlope(512, 684).xWeight == 4 * 512, "16:9 x weight");
  expect(widenedSlope(512, 684).zWeight == 3 * 684, "16:9 z weight");
  expect(widenedSlope(512, 512) == kRetailSlope, "a 4:3 plan keeps retail's slope");
  expect(widenedSlope(512, 400) == kRetailSlope, "a narrower plan never narrows the test");
  expect(widenedSlope(0, 684) == kRetailSlope, "an unlatched plan keeps retail's slope");

  // A sphere just beyond the 4:3 plane (|x| / z = 0.95 after margins) is culled at 4:3 and kept at
  // 16:9, whose half-angle tangent is 342 / 341.
  const ViewPoint margin{-1900 - margins.horizontalLateral, 0, 2000 - margins.horizontalDepth};
  expect(outsideHorizontal(margin, margins, kRetailSlope), "4:3 culls the margin sphere");
  expect(!outsideHorizontal(margin, margins, widenedSlope(512, 684)), "16:9 keeps it");

  // Classification: a small sphere on the axis is fully inside; one straddling the 4:3 plane is
  // inside only once the plane moves out.
  const auto small = sphereMargins(0x40);
  expect(insideFrustum(ViewPoint{0, 0, 2000}, small, kRetailSlope), "on-axis sphere is inside");
  expect(!insideFrustum(ViewPoint{1500, 0, 2000}, small, kRetailSlope),
         "a sphere at the 4:3 edge is not inside at 4:3");
  expect(insideFrustum(ViewPoint{1500, 0, 2000}, small, widenedSlope(512, 684)),
         "the same sphere is inside at 16:9");

  expect(behindEye(ViewPoint{0, 0, -0x41}, 0x40), "a sphere wholly behind the eye");
  expect(!behindEye(ViewPoint{0, 0, -0x3F}, 0x40), "a sphere crossing the eye plane");

  if (failures == 0) {
    std::printf("guest_moby_frustum: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
