// guest_terrain_split.h — what the terrain subdivision passes of this engine family share
// (SCUS_944.25 80025CC8..80028504): the split lists the near passes deferred polygons to, the grid
// of subdivided points each polygon is cut into, and the crack-filling triangles along its edges.
//
// A split list (kCoarseSplitList, kFineSplitList) is a run of sector header words, each followed by
// that sector's deferred polygon entries, ended by zero. A header is positive (the sector address
// / 4, top bit clear for a translucent sector); an entry is the polygon record's address with
// kSplitOverflow / kSplitTriangle in its low bits. The header restates what the near pass knew:
// the camera-relative origin, the vertex and colour arrays, how the height is packed, and whether
// the sector is exempt from fog (kFogExempt, parked in the light matrix's L33).
//
// THE GRID. A polygon is cut into a square grid of points in the scratchpad, one 16-byte cell each:
// first the model-space position (z, x, y halfwords at +0/+2/+4) and the vertex colour at +8, then,
// after projection, SXY at +0 and the depth with four outcode bits (0x1000 above, 0x2000 below,
// 0x4000 left, 0x8000 right) at +4. Points small enough are projected at 16x scale for precision.
//
// CRACKS. Where a neighbouring polygon was drawn at a coarser level, the shared edge's midpoints
// would open a crack, so each such edge is closed with GT3 triangles from a table of cell triples.
#pragma once

#include <cstdint>
#include <span>

namespace spyro::guest_terrain {

struct TerrainFrame;

inline constexpr std::uint32_t kCellSize = 0x10;
namespace cell {
inline constexpr std::uint32_t kZ = 0x0; // before projection
inline constexpr std::uint32_t kX = 0x2;
inline constexpr std::uint32_t kY = 0x4;
inline constexpr std::uint32_t kScreen = 0x0; // after projection: SXY
inline constexpr std::uint32_t kDepth = 0x4;  // after projection: SZ (0x000..0xFFF) | outcodes
inline constexpr std::uint32_t kColour = 0x8;
} // namespace cell

inline constexpr std::uint32_t kDepthMask = 0x0FFFu;
inline constexpr std::uint32_t kOutcodeMask = 0xF000u;
inline constexpr std::uint32_t kColourAverageMask = 0xFFFEFEFFu; // so (a + b) >> 1 cannot carry

// The sector a run of split-list entries belongs to.
struct SplitSector {
  std::uint32_t originZ = 0; // s7
  std::uint32_t originX = 0; // t8
  std::uint32_t originY = 0; // t9
  std::uint32_t heightShift = 0;
  std::uint32_t heightMask = 0;
  std::uint32_t vertices = 0; // s4
  std::uint32_t colours = 0;  // s5
};

// One unpacked vertex, in the 32-bit precision retail keeps it while it averages.
struct GridPoint {
  std::uint32_t z;
  std::uint32_t x;
  std::uint32_t y;
};

// The average retail takes of two grid coordinates: a 32-bit sum, shifted arithmetically.
[[nodiscard]] constexpr std::uint32_t midpoint(std::uint32_t a, std::uint32_t b) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(a + b) >> 1);
}

[[nodiscard]] constexpr GridPoint midpoint(const GridPoint &a, const GridPoint &b) {
  return GridPoint{midpoint(a.z, b.z), midpoint(a.x, b.x), midpoint(a.y, b.y)};
}

// The average of two colour words, after clearing each channel's low bit.
[[nodiscard]] constexpr std::uint32_t blend(std::uint32_t a, std::uint32_t b) {
  return ((a & kColourAverageMask) + (b & kColourAverageMask)) >> 1;
}

// One deferred polygon, as a pass walking a split list sees it.
struct SplitPolygon {
  std::uint32_t record; // the polygon record (guest_terrain_polygon.h)
  bool overflow;        // a vertex projected with overflow: the near pass skipped its backface test
  bool triangle;
};

class SplitPolygonPass {
public:
  virtual void polygon(const SplitSector &sector, const SplitPolygon &polygon) = 0;

protected:
  SplitPolygonPass() = default;
  SplitPolygonPass(const SplitPolygonPass &) = default;
  SplitPolygonPass &operator=(const SplitPolygonPass &) = default;
  ~SplitPolygonPass() = default;
};

// Walk the split list at `list` (80025D0C / 80026C88 / 80027AD4), reading each sector header
// (80025D24) and handing every entry under it to `pass`.
void walkSplitList(TerrainFrame &frame, std::uint32_t list, SplitPolygonPass &pass);

[[nodiscard]] GridPoint
unpackGridVertex(const TerrainFrame &frame, const SplitSector &sector, std::uint32_t index);

void storeGridPoint(TerrainFrame &frame, std::uint32_t cellIndex, const GridPoint &point);

// The polygon's corner colours, `count` of them in retail's fog order, each with the GP0 code
// added, then fogged when the level fogs and the sector is not exempt (80025F80).
void loadCornerColours(TerrainFrame &frame, std::uint32_t *colours, std::uint32_t count);

// Project cells 0..count-1 in place (800260BC). `markBehind` sets every outcode bit of a point
// whose depth is zero (the fine pass, 80027454).
void projectGrid(TerrainFrame &frame, std::uint32_t count, bool markBehind);

// Emit the crack triangle described by the 12-byte table entry at `entry` (80026338), unless its
// three cells share an outcode. `uv` / `page` are the polygon's texture words.
void emitCrackTriangle(TerrainFrame &frame,
                       std::uint32_t entry,
                       std::uint32_t colourAdjust,
                       std::uint32_t uv,
                       std::uint32_t page,
                       std::uint32_t draw);

// 80026230: pull a projected grid point toward the midpoint of two others, GPF-weighting its own
// SXY by `ownWeight` and the pair's sum by `pairWeight` (each / 0x1000, then / 0x100), so the
// subdivided surface of a near polygon does not bulge where perspective moves its inner points.
void pullTowardPair(TerrainFrame &frame,
                    std::uint32_t point,
                    std::uint32_t first,
                    std::uint32_t second,
                    std::uint32_t ownWeight,
                    std::uint32_t pairWeight);

// The backface test a subdivision pass gives a piece of an overflowed polygon (800264B0), whose
// whole the near pass could not test: two-sided pieces pass; otherwise the (first, second, third)
// winding decides, and a back-facing one is given a second chance on (fourth, second, third).
[[nodiscard]] bool facesCamera(TerrainFrame &frame,
                               std::uint32_t first,
                               std::uint32_t second,
                               std::uint32_t third,
                               std::uint32_t fourth,
                               std::uint32_t draw);
[[nodiscard]] bool triangleFacesCamera(TerrainFrame &frame,
                                       std::uint32_t first,
                                       std::uint32_t second,
                                       std::uint32_t third,
                                       std::uint32_t draw);

// Whether a GT3 (3 screen words) or GT4 (4) is too large for the GPU, which draws nothing 512 or
// more high or 1024 or more wide (80027944): retail tests the vertical spans of the edges and the
// quad's diagonal first, then the horizontal ones.
[[nodiscard]] bool exceedsGpuSpan(std::span<const std::uint32_t> screens);

// Which edges of the polygon border a neighbour the detail pass drew coarser: one byte per edge,
// each the neighbour's flag byte (1 drawn whole, 2 deferred coarse) masked with `bit`. A record's
// `links` word (+8) and draw word locate a quad's four neighbours (800262AC); a triangle's third
// is located by its colour word's low byte instead (80026954).
[[nodiscard]] std::uint32_t
quadNeighbourEdges(TerrainFrame &frame, std::uint32_t links, std::uint32_t draw, std::uint32_t bit);
[[nodiscard]] std::uint32_t triangleNeighbourEdges(TerrainFrame &frame,
                                                   std::uint32_t links,
                                                   std::uint32_t colourIndices,
                                                   std::uint32_t bit);

} // namespace spyro::guest_terrain
