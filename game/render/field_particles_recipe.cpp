#include "field_particles_recipe.h"

#include <cstddef>
#include <utility>

namespace spyro::field_particles_recipe {
namespace {

constexpr uint32_t kParticlePointer = 0x80075824u;
constexpr uint32_t kParticleTextures = 0x80076278u;
constexpr uint32_t kRecordSize = 0x20u;
constexpr uint32_t kRecordCapacity = 256u;

// The texture entry both textured arms resolve, identically: a class byte at record +0 selects a
// table out of 0x80076278, and the low byte of the halfword at +0x10 indexes an 8-byte entry in it
// whose second and third words are the packed uv/clut and uv/tpage. Its high byte is the depth
// bias. One decode, because one disagreement between the two arms would be a texture drawn from
// the wrong table on one particle type only — a defect nothing in a still frame would reveal.
struct TextureEntry {
  bool ok = false;
  const char *refusal = "";
  uint32_t uvClut = 0;
  uint32_t uvTpage = 0;
  uint8_t depthBias = 0;
};

TextureEntry textureEntry(const world_chunk_codec::RamView &ram, uint32_t address) {
  TextureEntry entry{};
  const uint32_t textureTableAddress = kParticleTextures + (uint32_t)ram.r8(address) * 4u;
  if (!ram.contains(textureTableAddress, 4u)) {
    entry.refusal = "texture_table_pointer";
    return entry;
  }
  const uint32_t textureTable = ram.r32(textureTableAddress);
  const uint16_t textureIndex = ram.r16(address + 0x10u);
  const uint32_t textureAddress = textureTable + (uint32_t)(textureIndex & 0xffu) * 8u;
  if (!ram.contains(textureAddress, 12u)) {
    entry.refusal = "texture_entry";
    return entry;
  }
  entry.ok = true;
  entry.uvClut = ram.r32(textureAddress + 4u);
  entry.uvTpage = ram.r32(textureAddress + 8u);
  entry.depthBias = (uint8_t)(textureIndex >> 8);
  return entry;
}

Recipe refuse(Recipe out, Status status, const char *why, int32_t type = -1, uint32_t address = 0) {
  out.status = status;
  out.refusal = why;
  out.refusedType = type;
  out.refusedAddress = address;
  out.points.clear();
  out.lines.clear();
  out.texturedQuads.clear();
  out.spriteQuads.clear();
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
    if (type == 1) {
      // Six contiguous halfwords hold the two endpoints in the same (x, y, z) order the type-0 arm
      // uses for its single point, so the first spans words 4/8 and the second words 8/0xC.
      const uint32_t xy0 = ram.r32(address + 4u);
      const uint32_t z0x1 = ram.r32(address + 8u);
      const uint32_t yz1 = ram.r32(address + 0xcu);
      const uint32_t color0 = ram.r32(address + 0x10u);
      const uint32_t color1 = ram.r32(address + 0x14u);
      out.lines.push_back(Line{address,
                               i,
                               (int16_t)xy0,
                               (int16_t)(xy0 >> 16),
                               (int16_t)z0x1,
                               (int16_t)(z0x1 >> 16),
                               (int16_t)yz1,
                               (int16_t)(yz1 >> 16),
                               (uint8_t)(color1 >> 24),
                               (uint8_t)color0,
                               (uint8_t)(color0 >> 8),
                               (uint8_t)(color0 >> 16),
                               (uint8_t)color1,
                               (uint8_t)(color1 >> 8),
                               (uint8_t)(color1 >> 16)});
      continue;
    }
    if (type == 2 || type == 3) {
      const TextureEntry texture = textureEntry(ram, address);
      if (!texture.ok) {
        return refuse(std::move(out), Status::InvalidPointers, texture.refusal);
      }
      const uint32_t xy = ram.r32(address + 4u);
      const uint32_t zAndSize = ram.r32(address + 8u);
      if (type == 2) {
        out.texturedQuads.push_back(TexturedQuad{address,
                                                 i,
                                                 ram.r8(address),
                                                 (int16_t)xy,
                                                 (int16_t)(xy >> 16),
                                                 (int16_t)zAndSize,
                                                 (uint8_t)(zAndSize >> 16),
                                                 (uint16_t)(((zAndSize >> 23) + 0x40u) & 0x1feu),
                                                 texture.depthBias,
                                                 ram.r32(address + 0xcu),
                                                 texture.uvClut,
                                                 texture.uvTpage});
        continue;
      }
      // Type 3 spends the same two size bits differently: where type 2 reads one size byte and an
      // angle out of the top nine bits, type 3 reads two independent extents.
      out.spriteQuads.push_back(SpriteQuad{address,
                                           i,
                                           ram.r8(address),
                                           (int16_t)xy,
                                           (int16_t)(xy >> 16),
                                           (int16_t)zAndSize,
                                           (uint8_t)(zAndSize >> 16),
                                           (uint8_t)(zAndSize >> 24),
                                           texture.depthBias,
                                           ram.r32(address + 0xcu),
                                           texture.uvClut,
                                           texture.uvTpage});
      continue;
    }
    if (type != 0) {
      return refuse(std::move(out), Status::UnsupportedType, "particle_type", type, address);
    }
    const uint32_t xy = ram.r32(address + 4u);
    const uint32_t zAndBias = ram.r32(address + 8u);
    const uint32_t color = ram.r32(address + 0xcu);
    out.points.push_back(Point{address,
                               i,
                               (int16_t)xy,
                               (int16_t)(xy >> 16),
                               (int16_t)zAndBias,
                               (uint8_t)(zAndBias >> 16),
                               (uint8_t)color,
                               (uint8_t)(color >> 8),
                               (uint8_t)(color >> 16)});
  }
  out.status = (out.points.empty() && out.lines.empty() && out.texturedQuads.empty() &&
                out.spriteQuads.empty())
                   ? Status::ValidEmpty
                   : Status::Ready;
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
