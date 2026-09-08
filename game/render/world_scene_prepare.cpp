#include "world_scene_prepare.h"

#include "wide_clip_plan.h"
#include "world_projection_math.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace spyro::world_scene_prepare {
namespace {

using psxport::native_projection::ProjectionParams;
using spyro::world_chunk_codec::RamView;

bool horizontalInside(int32_t extent, int32_t depth, int32_t width) {
  // The authored native plane is 4*x < 3*z. Projection retains focal length and
  // expands its centered viewport by width/512, so scale only the depth term.
  return 4ll * wide::kNativeClipWidth * extent < 3ll * width * depth;
}

bool broadCull(int32_t x, int32_t y, int32_t z, int32_t radius, int32_t width) {
  const int32_t hx = (radius >> 1) + (radius >> 2) + (radius >> 5);
  const int32_t xz = (radius >> 1) + (radius >> 4) + (radius >> 5);
  const int32_t hy = radius - (radius >> 3);
  const int32_t yz = (radius >> 1) - (radius >> 4);
  // Retain authored near-eye margin acceptance even when z+xz is negative: rotating
  // that approximate bound outward must not remove sectors the native view admitted.
  const bool horizontal = horizontalInside(std::abs(x) - hx, z + xz, wide::kNativeClipWidth) ||
                          horizontalInside(std::abs(x) - hx, z + xz, width);
  return z + radius > 0 && horizontal && 32 * (std::abs(y) - hy) - 17 * (z + yz) < 0;
}

bool whollyInside(int32_t x, int32_t y, int32_t z, int32_t radius, int32_t width) {
  const int32_t hx = (radius >> 1) + (radius >> 2) + (radius >> 5);
  const int32_t xz = (radius >> 1) + (radius >> 4) + (radius >> 5);
  const int32_t hy = radius - (radius >> 3);
  const int32_t yz = (radius >> 1) - (radius >> 4);
  return z - radius > 0 && horizontalInside(std::abs(x) + hx, z - xz, width) &&
         32 * (std::abs(y) + hy) - 17 * (z - yz) < 0;
}

} // namespace

bool prepare(const world_source::Selection &selection,
             int32_t horizontalWidth,
             Prepared &out,
             const char *&why,
             bool decodingAnimation) {
  out = {};
  if (horizontalWidth < wide::kNativeClipWidth || horizontalWidth > INT16_MAX) {
    why = "clip_width";
    return false;
  }
  if (!selection.valid) {
    why = selection.refusal;
    return false;
  }
  out.selectedSectors = (uint32_t)selection.occurrences.size();
  const auto &cullMatrix = selection.camera.cullingMatrix;
  const int32_t cameraX = selection.camera.position[0] >> 4;
  const int32_t cameraY = selection.camera.position[1] >> 4;
  const int32_t cameraZ = selection.camera.position[2] >> 4;
  const uint32_t lod = selection.lodDistance >> 4;
  const ProjectionParams unused{};
  for (uint8_t index : selection.occurrences) {
    if (!selection.sectors[index]) {
      why = "sector_bounds";
      return false;
    }
    const auto &header = *selection.sectors[index];
    const uint32_t sector = header.address;
    const uint32_t h0 = header.center, h1 = header.extent;
    const auto transformed =
        psxport::native_projection::project(cullMatrix,
                                            unused,
                                            {(int16_t)(cameraY - (int32_t)(uint16_t)h0),
                                             (int16_t)(cameraZ - (int32_t)(uint16_t)(h1 >> 16)),
                                             (int16_t)((int32_t)(uint16_t)(h0 >> 16) - cameraX)});
    const int32_t x = transformed.ir[0], y = transformed.ir[1], z = transformed.ir[2];
    const int32_t radius = h1 & 0x1fffu;
    if (!broadCull(x, y, z, radius, horizontalWidth)) {
      continue;
    }
    out.broadVisible[index] = 0xffu;
    uint8_t tags = whollyInside(x, y, z, radius, horizontalWidth) ? 0u : 1u;
    const uint32_t flags = h1 & 0xe000u;
    const bool low = !(flags & 0x2000u) && ((flags & 0x8000u) || (int32_t)lod < z + radius + 256);
    const bool high = !(flags & 0x4000u) && z - radius < (int32_t)lod;
    if (low) {
      out.low.push_back({sector, index, tags});
    }
    if (high) {
      if (z - radius < 256) {
        tags |= 2u;
      }
      out.high.push_back({sector, index, tags});
    }
    const uint32_t dirty = header.animation;
    const uint32_t activeMask =
        low ? (high ? 0u : 0xffff0000u) : (high ? 0x0000ffffu : 0xffffffffu);
    const uint32_t active = dirty | activeMask;
    if (decodingAnimation) {
      out.animations.push_back({sector, active});
      continue;
    }
    for (uint32_t channel = 0; channel < 4; ++channel) {
      if ((uint8_t)(active >> (channel * 8u)) < 0x80u) {
        why = "active_animation";
        return false;
      }
    }
  }
  return true;
}

bool prepare(const RamView &ram,
             int32_t selection,
             int32_t horizontalWidth,
             Prepared &out,
             const char *&why,
             world_animation::Plan *animation) {
  if (!prepare(
          world_source::select(ram, selection), horizontalWidth, out, why, animation != nullptr)) {
    return false;
  }
  if (animation != nullptr) {
    for (const auto &sector : out.animations) {
      if (!world_animation::appendSector(ram, sector.address, sector.active, *animation, why)) {
        return false;
      }
    }
  }
  return true;
}

} // namespace spyro::world_scene_prepare
