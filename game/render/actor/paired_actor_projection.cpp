#include "paired_actor_projection.h"

#include "native_projection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spyro::paired_actor_projection {
namespace {

using spyro::paired_actor::ProjectedVertex;

} // namespace

ProjectedVertex
projectRtps(std::uint32_t d0, std::uint32_t d1, const std::array<std::uint32_t, 27> &cr) {
  using namespace psxport::native_projection;
  const std::uint32_t c0 = cr[0], c1 = cr[1], c2 = cr[2], c3 = cr[3], c4 = cr[4];
  FixedAffine affine{};
  affine.m = rotationFromControlWords({c0, c1, c2, c3, c4});
  affine.t = {{(int32_t)cr[5], (int32_t)cr[6], (int32_t)cr[7]}};
  const NativeProjectedVertex p = project(affine,
                                          {(int32_t)cr[24], (int32_t)cr[25], (uint16_t)cr[26]},
                                          {(int16_t)d0, (int16_t)(d0 >> 16), (int16_t)d1});
  return {p.sx,
          p.sy,
          p.sz,
          p.pz,
          p.raw_view[2],
          p.raw_view[0],
          p.raw_view[1],
          p.px,
          p.py,
          (int16_t)p.ir[0],
          (int16_t)p.ir[1],
          (int16_t)p.ir[2]};
}

int roundScreen(float value) {
  return (int)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

} // namespace spyro::paired_actor_projection
