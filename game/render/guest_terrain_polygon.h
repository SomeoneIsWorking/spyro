// guest_terrain_polygon.h — the polygon records the two near terrain passes share (the
// detail pass 800249F8 and the translucent pass 800257C0), and the two things both do with one:
// defer it to a split list, or emit its texture words.
//
// A sector's polygons are 16-byte records after its colour arrays:
//
//   +0x0  four vertex indices as byte offsets (bits 22, 14, 6 and 0 of the word, each * 4); a
//         triangle repeats its third index as its fourth
//   +0x4  four colour indices, the same layout
//   +0x8  where the subdivision passes find its neighbours' flag bytes (guest_terrain_split.h)
//   +0xC  draw word: bits 0..6 texture record, 7..9 ordering-table bias, 0x400 flipped winding,
//         0x800 two-sided, 0x3000 which corner a triangle drops from its record's quad
//
// A polygon too near for one primitive (depth sum under 0x2000) is deferred to a split list as the
// address of its record (+1 when a vertex projected with overflow, +2 for a triangle), after a
// once-per-sector header word the subdivision passes use to find the sector again.
#pragma once

#include "guest_terrain_frame.h"

#include <cstdint>

namespace spyro::guest_terrain {

namespace polygon {
inline constexpr std::uint32_t kIndices = 0x0;
inline constexpr std::uint32_t kColours = 0x4;
inline constexpr std::uint32_t kLinks = 0x8;
inline constexpr std::uint32_t kDraw = 0xC;
inline constexpr std::uint32_t kSize = 0x10;
} // namespace polygon

// GP0 polygon packet slots.
namespace packet {
inline constexpr std::uint32_t kColour0 = 0x04;
inline constexpr std::uint32_t kCode = 0x07;
inline constexpr std::uint32_t kXy0 = 0x08;
inline constexpr std::uint32_t kUv0 = 0x0C;
inline constexpr std::uint32_t kColour1 = 0x10;
inline constexpr std::uint32_t kXy1 = 0x14;
inline constexpr std::uint32_t kUv1 = 0x18;
inline constexpr std::uint32_t kColour2 = 0x1C;
inline constexpr std::uint32_t kXy2 = 0x20;
inline constexpr std::uint32_t kUv2 = 0x24;
inline constexpr std::uint32_t kColour3 = 0x28;
inline constexpr std::uint32_t kXy3 = 0x2C;
inline constexpr std::uint32_t kUv3 = 0x30;
} // namespace packet

// The packets' GP0 tags (word count in the top byte) and command codes.
inline constexpr std::uint32_t kQuadTag = 0x0C000000u;
inline constexpr std::uint32_t kQuadBytes = 0x34;
inline constexpr std::uint32_t kTriangleTag = 0x09000000u;
inline constexpr std::uint32_t kTriangleBytes = 0x28;
inline constexpr std::uint32_t kOpaqueQuadCode = 0x3C000000u;     // GT4
inline constexpr std::uint32_t kOpaqueTriangleCode = 0x34000000u; // GT3
inline constexpr std::uint32_t kSemiQuadCode = 0x3E000000u;       // GT4, semi-transparent
inline constexpr std::uint32_t kSemiTriangleCode = 0x36000000u;   // GT3, semi-transparent

// The draw word's fields.
inline constexpr std::uint32_t kTextureRecordMask = 0x7Fu;
inline constexpr std::uint32_t kTextureRecordSize = 48;
inline constexpr std::uint32_t kBinBiasMask = 0x380u;
inline constexpr std::uint32_t kFlippedWinding = 0x400u;
inline constexpr std::uint32_t kTwoSided = 0x800u;
inline constexpr std::uint32_t kTriangleCornerMask = 0x3000u;

// The split-list entry bits.
inline constexpr std::uint32_t kSplitOverflow = 1;
inline constexpr std::uint32_t kSplitTriangle = 2;

// The per-sector "header written" bits the passes keep in the sector word's low bits.
inline constexpr std::uint32_t kCoarseHeaderWritten = 1;
inline constexpr std::uint32_t kFineHeaderWritten = 2;

// Four byte offsets packed at bits 22, 14, 6 and 0 (each stored / 4).
struct PackedIndices {
  explicit PackedIndices(std::uint32_t word)
      : first((word >> 22) & 0x3FCu), second((word >> 14) & 0x3FCu), third((word >> 6) & 0x3FCu),
        fourth((word << 2) & 0x3FCu) {}

  [[nodiscard]] bool triangle() const {
    return third == fourth;
  }

  std::uint32_t first;
  std::uint32_t second;
  std::uint32_t third;
  std::uint32_t fourth;
};

// Append `entry` to the split list at `cursor`, first writing `header` if this sector has not yet
// put anything on that list (`headerBit` in `sectorWord`).
inline void deferToSplitList(TerrainMemory &memory,
                             std::uint32_t &cursor,
                             std::uint32_t &sectorWord,
                             std::uint32_t headerBit,
                             std::uint32_t header,
                             std::uint32_t entry) {
  if ((sectorWord & headerBit) == 0) {
    memory.w32(cursor, header);
    cursor += 4;
  }
  sectorWord |= headerBit;
  memory.w32(cursor, entry);
  cursor += 4;
}

// 80025378 / 80025C14: a textured triangle's three UV words. A texture record spans one 32-texel
// square as two words, `uv` (one corner's UV with the CLUT above it) and `page` (the opposite
// corner's UV with the texture page above it); the triangle drops the corner its draw word names.
// Returns the value retail leaves in v1.
inline std::uint32_t writeTriangleUvs(TerrainMemory &memory,
                                      std::uint32_t primitive,
                                      std::uint32_t uv,
                                      std::uint32_t page,
                                      std::uint32_t corner) {
  switch (corner) {
  case 0x0000u:
    memory.w32(primitive + 0x0C, uv);
    memory.w32(primitive + 0x18, page - 0x1F00u);
    memory.w32(primitive + 0x24, uv + 0x1F00u);
    return uv + 0x1F00u;
  case 0x1000u:
    memory.w32(primitive + 0x0C, uv + 0x1Fu);
    memory.w32(primitive + 0x18, page);
    memory.w32(primitive + 0x24, uv);
    return uv + 0x1Fu;
  case 0x2000u:
    memory.w32(primitive + 0x0C, uv + 0x1F1Fu);
    memory.w32(primitive + 0x18, page - 0x1Fu);
    memory.w32(primitive + 0x24, uv + 0x1Fu);
    return uv + 0x1Fu;
  default:
    memory.w32(primitive + 0x0C, uv + 0x1F00u);
    memory.w32(primitive + 0x18, page - 0x1F1Fu);
    memory.w32(primitive + 0x24, page);
    return page - 0x1F1Fu;
  }
}

// The ordering-table bin of a polygon whose vertex depths sum to `depthSum`.
inline std::uint32_t polygonBin(std::uint32_t depthSum, std::uint32_t draw) {
  return (depthSum >> 7) + ((draw & kBinBiasMask) >> 5);
}

} // namespace spyro::guest_terrain
