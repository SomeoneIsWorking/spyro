// guest_moby_frustum.h — the sphere-against-view-frustum arithmetic of the moby visibility walk of
// this engine family (see guest_moby_visibility.h), with no Core and no GTE.
//
// Retail tests a moby's bounding sphere, already in view space (x right, y down, z forward),
// against four planes through the eye. Every plane test is the same integer shape with a sphere
// margin on each side; only the weights differ:
//
//   horizontal   4 * (|x| - lateral) - 3 * (z + depth)  >= 0  -> outside   (SCUS_944.25
//   80043A74..80043A8C) vertical         (z + depth) - 3 * (|y| - lateral) <= 0  -> outside
//   (SCUS_944.25 80043ADC..80043AFC)
//
// 4:3 is the authored horizontal half-angle: tan = 3/4 = 256/341, half the 512-dot window over the
// projection distance H = 341 (0x155, the display bootstrap's distance publication). 1:3 is the
// vertical one (120/341). The "fully inside" classification flips both margins and the comparison.
//
// WIDESCREEN. The vertical weights and every margin stay retail's. The horizontal half-angle grows
// with the plan's projection width: the z weight is scaled by projectionWidth / nativeWidth,
// written as integer weights (4 * native, 3 * projection) so 684/512 is exact. At 4:3 the weights
// are the literal (4, 3) and the tests compute exactly what retail computes.
#pragma once

#include <cstdint>
#include <cstdlib>

namespace spyro::guest_moby_frustum {

// The two weights of the horizontal plane test: reject when xWeight*|x| - zWeight*z >= 0 after the
// margins. Retail's are (4, 3).
struct HorizontalSlope {
  std::int64_t xWeight = 4;
  std::int64_t zWeight = 3;

  friend constexpr bool operator==(const HorizontalSlope &, const HorizontalSlope &) = default;
};

inline constexpr HorizontalSlope kRetailSlope{4, 3};

// The slope for a projection `projectionWidth` columns wide whose authored window is `nativeWidth`.
// Never narrower than retail: a plan that does not widen yields exactly `kRetailSlope`.
constexpr HorizontalSlope widenedSlope(int nativeWidth, int projectionWidth) {
  if (nativeWidth <= 0 || projectionWidth <= nativeWidth) {
    return kRetailSlope;
  }
  return HorizontalSlope{kRetailSlope.xWeight * nativeWidth,
                         kRetailSlope.zWeight * projectionWidth};
}

// A bounding sphere's plane margins, derived from its radius by retail's shift sums. The horizontal
// pair approximates r / cos and r * tan of the 4:3 half-angle (0.78r, 0.59r); the vertical pair
// those of the 1:3 one (0.56r, 0.31r).
struct SphereMargins {
  std::int32_t horizontalLateral = 0; // r/2 + r/4 + r/32
  std::int32_t horizontalDepth = 0;   // r/2 + r/16 + r/32
  std::int32_t verticalLateral = 0;   // r/2 + r/16
  std::int32_t verticalDepth = 0;     // r/4 + r/16
};

constexpr SphereMargins sphereMargins(std::uint32_t radius) {
  return SphereMargins{
      .horizontalLateral = static_cast<std::int32_t>((radius >> 1) + (radius >> 2) + (radius >> 5)),
      .horizontalDepth = static_cast<std::int32_t>((radius >> 1) + (radius >> 4) + (radius >> 5)),
      .verticalLateral = static_cast<std::int32_t>((radius >> 1) + (radius >> 4)),
      .verticalDepth = static_cast<std::int32_t>((radius >> 2) + (radius >> 4)),
  };
}

// A view-space sphere centre, as the GTE leaves it in MAC1..MAC3.
struct ViewPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

// The whole sphere is behind the eye.
constexpr bool behindEye(const ViewPoint &p, std::uint32_t radius) {
  return p.z + static_cast<std::int32_t>(radius) <= 0;
}

constexpr bool
outsideHorizontal(const ViewPoint &p, const SphereMargins &m, const HorizontalSlope &slope) {
  const std::int64_t lateral = std::abs(static_cast<std::int64_t>(p.x)) - m.horizontalLateral;
  const std::int64_t depth = static_cast<std::int64_t>(p.z) + m.horizontalDepth;
  return slope.xWeight * lateral - slope.zWeight * depth >= 0;
}

constexpr bool outsideVertical(const ViewPoint &p, const SphereMargins &m) {
  const std::int64_t lateral = std::abs(static_cast<std::int64_t>(p.y)) - m.verticalLateral;
  const std::int64_t depth = static_cast<std::int64_t>(p.z) + m.verticalDepth;
  return depth - 3 * lateral <= 0;
}

// The whole sphere is inside both plane pairs, so its polygons need no edge handling.
constexpr bool
insideFrustum(const ViewPoint &p, const SphereMargins &m, const HorizontalSlope &slope) {
  const std::int64_t xLateral = std::abs(static_cast<std::int64_t>(p.x)) + m.horizontalLateral;
  const std::int64_t xDepth = static_cast<std::int64_t>(p.z) - m.horizontalDepth;
  if (slope.xWeight * xLateral - slope.zWeight * xDepth >= 0) {
    return false;
  }
  const std::int64_t yLateral = std::abs(static_cast<std::int64_t>(p.y)) + m.verticalLateral;
  const std::int64_t yDepth = static_cast<std::int64_t>(p.z) - m.verticalDepth;
  return yDepth - 3 * yLateral > 0;
}

} // namespace spyro::guest_moby_frustum
