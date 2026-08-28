#include "world_scene_prepare.h"

#include "world_projection_math.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace spyro::world_scene_prepare {
namespace {

using psxport::native_projection::ProjectionParams;
using spyro::world_chunk_codec::RamView;

constexpr uint32_t kEnvironment = 0x800785A8u;
constexpr uint32_t kCamera = 0x80076DD0u;

bool mapped(const RamView &ram, uint32_t address, uint32_t size) {
  return ram.contains(address, size);
}

bool broadCull(int32_t x, int32_t y, int32_t z, int32_t radius) {
  const int32_t hx = (radius >> 1) + (radius >> 2) + (radius >> 5);
  const int32_t xz = (radius >> 1) + (radius >> 4) + (radius >> 5);
  const int32_t hy = radius - (radius >> 3);
  const int32_t yz = (radius >> 1) - (radius >> 4);
  return z + radius > 0 && 4 * (std::abs(x) - hx) - 3 * (z + xz) < 0 &&
         32 * (std::abs(y) - hy) - 17 * (z + yz) < 0;
}

bool whollyInside(int32_t x, int32_t y, int32_t z, int32_t radius) {
  const int32_t hx = (radius >> 1) + (radius >> 2) + (radius >> 5);
  const int32_t xz = (radius >> 1) + (radius >> 4) + (radius >> 5);
  const int32_t hy = radius - (radius >> 3);
  const int32_t yz = (radius >> 1) - (radius >> 4);
  return z - radius > 0 && 4 * (std::abs(x) + hx) - 3 * (z - xz) < 0 &&
         32 * (std::abs(y) + hy) - 17 * (z - yz) < 0;
}

} // namespace

bool prepare(const RamView &ram,
             int32_t selection,
             Prepared &out,
             const char *&why,
             world_animation::Plan *animation) {
  out = {};
  if (!mapped(ram, kEnvironment, 44u) || !mapped(ram, kCamera, 52u)) {
    why = "global_bounds";
    return false;
  }
  const uint32_t sectorTable = ram.r32(kEnvironment);
  const uint32_t sectorCount = ram.r32(kEnvironment + 4u);
  if (sectorCount > 256u || (sectorCount && !mapped(ram, sectorTable, sectorCount * 4u))) {
    why = "sector_table";
    return false;
  }
  std::vector<uint8_t> indices;
  if (selection < 0) {
    indices.reserve(sectorCount);
    for (uint32_t i = 0; i < sectorCount; ++i) {
      indices.push_back((uint8_t)i);
    }
  } else {
    const uint32_t groups = ram.r32(kEnvironment + 8u);
    const uint32_t slot = groups + (uint32_t)selection * 4u;
    if (!mapped(ram, slot, 4u)) {
      why = "occlusion_group_slot";
      return false;
    }
    uint32_t cursor = ram.r32(slot);
    for (uint32_t guard = 0; guard <= 256u; ++guard) {
      if (!mapped(ram, cursor, 1u)) {
        why = "occlusion_group_bounds";
        return false;
      }
      const uint8_t index = ram.r8(cursor++);
      if (index == 0xffu) {
        break;
      }
      if (index >= sectorCount) {
        why = "occlusion_sector_index";
        return false;
      }
      indices.push_back(index);
      if (guard == 256u) {
        why = "occlusion_group_unterminated";
        return false;
      }
    }
  }

  out.selectedSectors = (uint32_t)indices.size();
  const auto cullMatrix = world_projection_math::decodeMatrix(ram, kCamera + 0x14u);
  const int32_t cameraX = (int32_t)ram.r32(kCamera + 0x28u) >> 4;
  const int32_t cameraY = (int32_t)ram.r32(kCamera + 0x2cu) >> 4;
  const int32_t cameraZ = (int32_t)ram.r32(kCamera + 0x30u) >> 4;
  const uint32_t lod = ram.r32(kEnvironment + 0x24u) >> 4;
  const ProjectionParams unused{};
  for (uint8_t index : indices) {
    const uint32_t sector = ram.r32(sectorTable + (uint32_t)index * 4u);
    if ((sector & 3u) || !mapped(ram, sector, 0x1cu)) {
      why = "sector_bounds";
      return false;
    }
    const uint32_t h0 = ram.r32(sector), h1 = ram.r32(sector + 4u);
    const auto transformed =
        psxport::native_projection::project(cullMatrix,
                                            unused,
                                            {(int16_t)(cameraY - (int32_t)(uint16_t)h0),
                                             (int16_t)(cameraZ - (int32_t)(uint16_t)(h1 >> 16)),
                                             (int16_t)((int32_t)(uint16_t)(h0 >> 16) - cameraX)});
    const int32_t x = transformed.ir[0], y = transformed.ir[1], z = transformed.ir[2];
    const int32_t radius = h1 & 0x1fffu;
    if (!broadCull(x, y, z, radius)) {
      continue;
    }
    out.broadVisible[index] = 0xffu;
    uint8_t tags = whollyInside(x, y, z, radius) ? 0u : 1u;
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
    const uint32_t dirty = ram.r32(sector + 0x18u);
    const uint32_t activeMask =
        low ? (high ? 0u : 0xffff0000u) : (high ? 0x0000ffffu : 0xffffffffu);
    const uint32_t active = dirty | activeMask;
    if (animation != nullptr) {
      if (!world_animation::appendSector(ram, sector, active, *animation, why)) {
        return false;
      }
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

} // namespace spyro::world_scene_prepare
