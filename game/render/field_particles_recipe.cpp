#include "field_particles_recipe.h"

#include <cstddef>
#include <utility>

namespace spyro::field_particles_recipe {
namespace {

constexpr uint32_t kParticlePointer = 0x80075824u;
constexpr uint32_t kParticleTextures = 0x80076278u;
constexpr uint32_t kRecordSize = 0x20u;
constexpr uint32_t kRecordCapacity = 256u;

Recipe refuse(Recipe out, Status status, const char *why) {
  out.status = status;
  out.refusal = why;
  out.points.clear();
  out.texturedQuads.clear();
  return out;
}

} // namespace

Recipe derive(const world_chunk_codec::RamView &ram) {
  Recipe out{};
  if (!ram.contains(kParticlePointer, 4u)) {
    return refuse(std::move(out), Status::InvalidPointers, "pointer_globals");
  }
  const uint32_t base = ram.r32(kParticlePointer);
  // The guest renderer does not use g_ParticleAllocPtr as a list end. The allocator moves that
  // cursor through reusable slots and may wrap it; func_800573C8 instead scans from g_Particles
  // until the first type -1 terminator (skipping type -2 free slots). The extra four bytes cover
  // the sentinel written at g_Particles[256] during level initialization.
  if (!ram.contains(base, kRecordCapacity * kRecordSize + 4u)) {
    return refuse(std::move(out), Status::InvalidPointers, "list_bounds");
  }
  out.points.reserve(kRecordCapacity);
  for (uint32_t i = 0; i <= kRecordCapacity; ++i) {
    const uint32_t address = base + i * kRecordSize;
    const int8_t type = static_cast<int8_t>(ram.r8(address + 1u));
    if (type == -1) {
      break;
    }
    if (i == kRecordCapacity) {
      return refuse(std::move(out), Status::InvalidPointers, "missing_terminator");
    }
    ++out.records;
    if (type == -2) {
      continue;
    }
    if (type == 2) {
      const uint32_t textureTableAddress = kParticleTextures + (uint32_t)ram.r8(address) * 4u;
      if (!ram.contains(textureTableAddress, 4u)) {
        return refuse(std::move(out), Status::InvalidPointers, "texture_table_pointer");
      }
      const uint32_t textureTable = ram.r32(textureTableAddress);
      const uint16_t textureIndex = ram.r16(address + 0x10u);
      const uint32_t textureAddress = textureTable + (uint32_t)(textureIndex & 0xffu) * 8u;
      if (!ram.contains(textureAddress, 12u)) {
        return refuse(std::move(out), Status::InvalidPointers, "texture_entry");
      }
      const uint32_t xy = ram.r32(address + 4u);
      const uint32_t zAndSizeAngle = ram.r32(address + 8u);
      out.texturedQuads.push_back(TexturedQuad{address,
                                               ram.r8(address),
                                               (int16_t)xy,
                                               (int16_t)(xy >> 16),
                                               (int16_t)zAndSizeAngle,
                                               (uint8_t)(zAndSizeAngle >> 16),
                                               (uint16_t)(((zAndSizeAngle >> 23) + 0x40u) & 0x1feu),
                                               (uint8_t)(textureIndex >> 8),
                                               ram.r32(address + 0xcu),
                                               ram.r32(textureAddress + 4u),
                                               ram.r32(textureAddress + 8u)});
      continue;
    }
    if (type != 0) {
      return refuse(std::move(out), Status::UnsupportedType, "particle_type");
    }
    const uint32_t xy = ram.r32(address + 4u);
    const uint32_t zAndBias = ram.r32(address + 8u);
    const uint32_t color = ram.r32(address + 0xcu);
    out.points.push_back(Point{address,
                               (int16_t)xy,
                               (int16_t)(xy >> 16),
                               (int16_t)zAndBias,
                               (uint8_t)(zAndBias >> 16),
                               (uint8_t)color,
                               (uint8_t)(color >> 8),
                               (uint8_t)(color >> 16)});
  }
  out.status =
      (out.points.empty() && out.texturedQuads.empty()) ? Status::ValidEmpty : Status::Ready;
  return out;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid_empty";
  case Status::InvalidPointers:
    return "invalid_pointers";
  case Status::UnsupportedType:
    return "unsupported_type";
  }
  return "unknown";
}

} // namespace spyro::field_particles_recipe
