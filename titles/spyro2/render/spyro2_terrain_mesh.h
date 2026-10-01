// spyro2_terrain_mesh.h — how Spyro 2's two near terrain passes (the detail pass 800245D0 and the
// translucent pass 80025440) project one sector's vertices before they draw its polygons.
//
// A listed sector word is the sector's address with two classification bits in its low bits (see
// spyro2_terrain_classify.cpp): bit 0 when its bounding sphere touches a screen edge, bit 1 when it
// reaches within 0x100 of the camera. Its header locates a packed vertex array, and each vertex is
// rotated and projected with RTPS (camera RT, TR left at zero) relative to the sector origin minus
// the camera position the drawer parked in the light matrix.
//
// Every vertex leaves two things in the scratchpad: a word at kScratchpad + 4 * i and its depth SZ
// as a halfword at kVertexDepths + 2 * i. What the word holds depends on the sector's class:
//
//   inside   the projected SXY itself; the polygons are drawn without clip tests;
//   edge     SXY << 5 with four outcode bits (1 above, 2 below, 0xC left or right of the window);
//   close    as edge, but left and right are separate bits (4, 8), +0x10 when the projection
//            overflowed (the detail pass only), and a vertex nearer than 0x100 whose |MAC1|, |MAC2|
//            are under 0x100 is projected again at 16x scale so its SXY does not collapse.
//
// A sector whose vertices are all outside one edge is skipped whole, as is a close sector whose
// depths sum to zero (every vertex behind the camera).
#pragma once

#include <cstdint>

namespace spyro2::terrain {

struct TerrainFrame;

// The two near passes unpack the vertex height differently and run the close loop with different
// bookkeeping; everything else about a sector's projection is shared.
enum class NearPass {
  Detail,      // 800245D0: height (w << 3) & 0x1FFC; close vertices report projection overflow
  Translucent, // 80025440: height w & 0x3FF; no overflow bit, and deep vertices are not summed
};

// The sector header fields the near passes read.
namespace near_sector {
inline constexpr std::uint32_t kOriginZX = 0x08; // VZ origin << 14 in the high bits, VX / 4 low
inline constexpr std::uint32_t kOriginY = 0x0C;  // VY origin << 14 high; low: flag-byte offset
inline constexpr std::uint32_t kFog = 0x10;      // negative: the sector is never fogged
inline constexpr std::uint32_t kLayout = 0x14;   // vertex skip, colour count, polygon count
inline constexpr std::uint32_t kData = 0x1C;
} // namespace near_sector

inline constexpr std::uint32_t kSectorClassMask = 3;
inline constexpr std::uint32_t kEdgeSector = 1;
inline constexpr std::uint32_t kCloseSector = 2;

// The outcode bit a close vertex carries when its projection overflowed (detail pass only).
inline constexpr std::uint32_t kOverflowOutcode = 0x10;

struct ProjectedSector {
  bool drawn = false;          // false: the whole sector is off screen
  bool clipped = false;        // the scratchpad words are outcodes, not SXY
  bool overflowed = false;     // a close vertex's projection set the GTE error flag
  std::uint32_t vertexEnd = 0; // s0: two words past the last vertex word
};

// Project the sector at `sector` (class bits already removed) into the scratchpad.
ProjectedSector
projectSector(TerrainFrame &frame, std::uint32_t sector, std::uint32_t sectorClass, NearPass pass);

} // namespace spyro2::terrain
