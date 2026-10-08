#include "actor_billboard_face.h"

namespace spyro::actor_billboard {

Extents extents(const psxport::native_projection::ProjectionParams &projection,
                const psxport::native_projection::NativeProjectedVertex &centre,
                uint32_t material) {
  psxport::native_projection::ProjectionParams cueProjection = projection;
  cueProjection.dqa = 0x100;
  cueProjection.dqb = 0;
  // Every rotation term is zero and the translation carries only Z, so RTPS leaves MAC3 — and the
  // SZ3 the divide reads — equal to the centre's own depth.
  psxport::native_projection::FixedAffine depthOnly{};
  depthOnly.t[2] = (int32_t)centre.sz;
  const auto cue = psxport::native_projection::project(depthOnly, cueProjection, {0, 0, 0});
  // `mtc2` writes IR0's 16 bits and reads sign-extend them; the guest never asks hardware to clamp
  // this to 0..1000h, so neither does this.
  const int32_t ir0 = (int16_t)(uint16_t)((uint32_t)cue.mac0 >> 12);
  const uint32_t width = (uint32_t)(ir0 * halfWidth(material)) >> 11;
  const uint32_t height = (uint32_t)(ir0 * halfHeight(material)) >> 11;
  Extents out{};
  out.right = centre.sx + (int32_t)(width >> 1);
  out.left = out.right - (int32_t)width;
  out.bottom = centre.sy + (int32_t)(height >> 1);
  out.top = out.bottom - (int32_t)height;
  return out;
}

} // namespace spyro::actor_billboard
