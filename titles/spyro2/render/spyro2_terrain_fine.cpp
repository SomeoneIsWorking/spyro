// spyro2_terrain_fine.cpp — the fine subdivision passes of Spyro 2's terrain drawer (SCUS_944.25
// 80026C74..80028504): polygons the near passes deferred because a vertex came nearer than 0x140,
// each cut 4x4.
//
// The quad pass (80026C74) walks kFineSplitList skipping triangles; the triangle pass (80027AC8)
// walks it again skipping quads. Each polygon becomes a 5x5 (quad) or 15-point triangular grid
// (spyro2_terrain_split.h), projected with points at zero depth marked off every edge. A quad's
// four inner diagonal points are pulled toward their neighbours when every corner is past 0x40.
// Edges whose neighbour was deferred coarse get two crack triangles each, and every sub-polygon
// that is too large for the GPU (0x200 high or 0x400 wide) is still drawn but also recorded in
// the re-split list at kSplitPrimitiveCursor for the pass after this one.

#include "core.h"
#include "spyro2_terrain_frame.h"
#include "spyro2_terrain_passes.h"
#include "spyro2_terrain_polygon.h"
#include "spyro2_terrain_split.h"

#include <array>
#include <cstddef>

namespace spyro2::terrain {
namespace {

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

// ── Tables in the executable's data ──────────────────────────────────────────────────────────────
constexpr std::uint32_t kQuadPieces = 0x80061F70u;     // 8002775C: 16 first-cell words | quadrant
constexpr std::uint32_t kQuadCracks = 0x80061FE0u;     // 80027608: two triangles per edge
constexpr std::uint32_t kInnerPoints = 0x80062080u;    // 80027514: the 4 points refined
constexpr std::uint32_t kPageRemaps = 0x80062040u;     // 80027824
constexpr std::uint32_t kTrianglePieces = 0x800620A0u; // 80028248: 16 cell-triple words
constexpr std::uint32_t kTriangleCracks = 0x80062170u; // 800280F4: 0x48 per dropped corner
constexpr std::uint32_t kPieceTexture = 0x800622A0u;   // 80028254: signed bytes
constexpr std::uint32_t kPieceShift = 0x800622E0u;     // 800282C4: UV/page shifts
constexpr std::uint32_t kTriangleUvs = 0x80062460u;    // 8002825C: 0x40 per corner

constexpr std::uint32_t kCrackEntrySize = 12;
constexpr std::uint32_t kCracksPerEdge = 2;
constexpr std::uint32_t kGt4ToGt3 = 0x08000000u;
constexpr std::uint32_t kInnerRefineDepth = 0x40;
constexpr std::uint32_t kInnerRefineSpan = 0x100;
constexpr std::uint32_t kNeighbourDeferredCoarse = 2;
constexpr std::uint32_t kUvAverageMask = 0xFEFFu;

constexpr std::uint32_t kQuadGridCells = 25;
constexpr std::uint32_t kTriangleGridCells = 15;
constexpr std::uint32_t kRowStride = 5 * kCellSize;

std::uint32_t cellAddress(std::uint32_t index) {
  return kScratchpad + index * kCellSize;
}

// One coordinate (or colour) over the 5x5 grid, corners at cells 0, 4, 20, 24 (80026DB4).
template <typename Average>
std::array<std::uint32_t, kQuadGridCells>
quadGrid(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, Average average) {
  std::array<std::uint32_t, kQuadGridCells> g{};
  g[0] = a;
  g[4] = b;
  g[20] = c;
  g[24] = d;
  g[2] = average(a, b);
  g[1] = average(a, g[2]);
  g[3] = average(b, g[2]);
  g[22] = average(c, d);
  g[21] = average(c, g[22]);
  g[23] = average(d, g[22]);
  g[10] = average(a, c);
  g[14] = average(b, d);
  g[12] = average(g[10], g[14]);
  g[11] = average(g[10], g[12]);
  g[13] = average(g[14], g[12]);
  g[5] = average(a, g[10]);
  g[9] = average(b, g[14]);
  g[15] = average(c, g[10]);
  g[19] = average(d, g[14]);
  g[7] = average(g[5], g[9]);
  g[6] = average(g[5], g[7]);
  g[8] = average(g[9], g[7]);
  g[17] = average(g[15], g[19]);
  g[16] = average(g[15], g[17]);
  g[18] = average(g[19], g[17]);
  return g;
}

// One coordinate over the 15-point triangle grid, rows of 5, 4, 3, 2, 1 (80027BE0).
template <typename Average>
std::array<std::uint32_t, kTriangleGridCells>
triangleGrid(std::uint32_t a, std::uint32_t b, std::uint32_t c, Average average) {
  std::array<std::uint32_t, kTriangleGridCells> g{};
  g[0] = a;
  g[4] = b;
  g[14] = c;
  g[2] = average(a, b);
  g[1] = average(a, g[2]);
  g[3] = average(b, g[2]);
  g[9] = average(a, c);
  g[5] = average(a, g[9]);
  g[12] = average(c, g[9]);
  g[11] = average(b, c);
  g[8] = average(b, g[11]);
  g[13] = average(c, g[11]);
  g[6] = average(a, g[11]);
  g[7] = average(b, g[9]);
  g[10] = average(c, g[2]);
  return g;
}

std::uint32_t coordinateMidpoint(std::uint32_t a, std::uint32_t b) {
  return midpoint(a, b);
}

std::uint32_t colourMidpoint(std::uint32_t a, std::uint32_t b) {
  return blend(a, b);
}

template <std::size_t N>
void storeComponent(Core &core, std::uint32_t offset, const std::array<std::uint32_t, N> &grid) {
  for (std::size_t i = 0; i < N; ++i) {
    core.mem_w16(cellAddress(static_cast<std::uint32_t>(i)) + offset,
                 static_cast<std::uint16_t>(grid[i]));
  }
}

template <std::size_t N> void storeColours(Core &core, const std::array<std::uint32_t, N> &grid) {
  for (std::size_t i = 0; i < N; ++i) {
    core.mem_w32(cellAddress(static_cast<std::uint32_t>(i)) + cell::kColour, grid[i]);
  }
}

class FineSplitPass final : public SplitPolygonPass {
public:
  FineSplitPass(TerrainFrame &frame, bool triangles)
      : frame_(frame), core_(frame.core), triangles_(triangles) {}

  void polygon(const SplitSector &sector, const SplitPolygon &polygon) override;

private:
  void quad(const SplitSector &sector, const SplitPolygon &polygon);
  void triangle(const SplitSector &sector, const SplitPolygon &polygon);
  void refineInnerPoints();
  void drawCracks(std::uint32_t edges,
                  std::uint32_t table,
                  std::uint32_t colourAdjust,
                  std::uint32_t uv,
                  std::uint32_t page,
                  std::uint32_t draw);
  void drawQuadPieces(const SplitPolygon &polygon, std::uint32_t texture, std::uint32_t draw);
  void drawTrianglePieces(const SplitPolygon &polygon, std::uint32_t texture, std::uint32_t draw);
  void linkPiece(
      std::uint32_t sum, std::uint32_t draw, std::uint32_t tag, std::uint32_t bytes, bool resplit);

  TerrainFrame &frame_;
  Core &core_;
  bool triangles_;
};

void FineSplitPass::polygon(const SplitSector &sector, const SplitPolygon &polygon) {
  if (polygon.triangle != triangles_) {
    return;
  }
  if (polygon.triangle) {
    triangle(sector, polygon);
  } else {
    quad(sector, polygon);
  }
}

// 80026D40.
void FineSplitPass::quad(const SplitSector &sector, const SplitPolygon &polygon) {
  const PackedIndices vertices(core_.mem_r32(polygon.record + polygon::kIndices));
  const GridPoint a = unpackGridVertex(frame_, sector, vertices.first);
  const GridPoint b = unpackGridVertex(frame_, sector, vertices.second);
  const GridPoint c = unpackGridVertex(frame_, sector, vertices.fourth);
  const GridPoint d = unpackGridVertex(frame_, sector, vertices.third);
  const std::uint32_t links = core_.mem_r32(polygon.record + polygon::kLinks);
  const std::uint32_t draw = core_.mem_r32(polygon.record + polygon::kDraw);
  storeComponent(core_, cell::kZ, quadGrid(a.z, b.z, c.z, d.z, coordinateMidpoint));
  storeComponent(core_, cell::kX, quadGrid(a.x, b.x, c.x, d.x, coordinateMidpoint));
  storeComponent(core_, cell::kY, quadGrid(a.y, b.y, c.y, d.y, coordinateMidpoint));

  // 8002713C: the corner colours, fogged, blended over the grid; the centre blends the second and
  // fourth corners, the diagonal, and the inner points average toward it.
  const PackedIndices colourIndices(core_.mem_r32(polygon.record + polygon::kColours));
  const std::uint32_t texture = frame_.textures + (draw & kTextureRecordMask) * kTextureRecordSize;
  const std::uint32_t page = core_.mem_r32(texture + 4);
  const std::uint32_t code = asSigned(page) > 0 ? kOpaqueQuadCode : kSemiQuadCode;
  std::array<std::uint32_t, 4> colours = {
      core_.mem_r32(sector.colours + colourIndices.first) + code,
      core_.mem_r32(sector.colours + colourIndices.second) + code,
      core_.mem_r32(sector.colours + colourIndices.fourth) + code,
      core_.mem_r32(sector.colours + colourIndices.third) + code};
  loadCornerColours(frame_, colours.data(), 4);
  std::array<std::uint32_t, kQuadGridCells> colourGrid =
      quadGrid(colours[0], colours[1], colours[2], colours[3], colourMidpoint);
  // Retail's colour grid differs from the coordinate grid in its middle: the centre is the
  // diagonal blend, and the inner ring blends each corner and edge midpoint with the centre.
  colourGrid[12] = blend(colours[1], colours[2]);
  colourGrid[7] = blend(colourGrid[12], colourGrid[2]);
  colourGrid[11] = blend(colourGrid[12], colourGrid[10]);
  colourGrid[13] = blend(colourGrid[12], colourGrid[14]);
  colourGrid[17] = blend(colourGrid[12], colourGrid[22]);
  colourGrid[6] = blend(colours[0], colourGrid[12]);
  colourGrid[8] = blend(colours[1], colourGrid[12]);
  colourGrid[16] = blend(colours[2], colourGrid[12]);
  colourGrid[18] = blend(colours[3], colourGrid[12]);
  storeColours(core_, colourGrid);

  projectGrid(frame_, kQuadGridCells, true);
  refineInnerPoints();
  const std::uint32_t uv = core_.mem_r32(texture);
  drawCracks(quadNeighbourEdges(frame_, links, draw, kNeighbourDeferredCoarse),
             kQuadCracks,
             kGt4ToGt3,
             uv,
             page,
             draw);
  drawQuadPieces(polygon, texture, draw);
}

// 80027498: when every corner is past 0x40, the four inner diagonal points are pulled toward the
// points one row above and below them, by how near the nearest corner is.
void FineSplitPass::refineInnerPoints() {
  constexpr std::array<std::uint32_t, 4> kCorners = {0, 4, 20, 24};
  std::array<std::uint32_t, 4> past{};
  for (std::size_t i = 0; i < kCorners.size(); ++i) {
    past[i] =
        (core_.mem_r32(cellAddress(kCorners[i]) + cell::kDepth) & kDepthMask) - kInnerRefineDepth;
    if (asSigned(past[i]) <= 0) {
      return;
    }
  }
  std::uint32_t nearest = past[0];
  for (std::size_t i = 1; i < past.size(); ++i) {
    if (asSigned(past[i] - nearest) < 0) {
      nearest = past[i];
    }
  }
  for (std::uint32_t k = 0; k < 4; ++k) {
    const std::uint32_t point = core_.mem_r32(kInnerPoints + 4 * k);
    if ((core_.mem_r32(point + cell::kDepth) & kOutcodeMask) != 0) {
      continue;
    }
    pullTowardPair(frame_,
                   point,
                   point - 4 * kCellSize,
                   point + 4 * kCellSize,
                   kInnerRefineSpan - nearest,
                   nearest >> 1);
  }
}

// 8002763C: two crack triangles per edge whose neighbour byte is set.
void FineSplitPass::drawCracks(std::uint32_t edges,
                               std::uint32_t table,
                               std::uint32_t colourAdjust,
                               std::uint32_t uv,
                               std::uint32_t page,
                               std::uint32_t draw) {
  std::uint32_t entry = table;
  while (edges != 0) {
    const std::uint32_t edge = edges & 0xFFu;
    edges >>= 8;
    entry += kCracksPerEdge * kCrackEntrySize;
    if (edge == 0) {
      continue;
    }
    for (std::uint32_t i = kCracksPerEdge; i > 0; --i) {
      emitCrackTriangle(frame_, entry - i * kCrackEntrySize, colourAdjust, uv, page, draw);
    }
  }
}

// Link a piece, first recording it for the re-split pass when it is too large for the GPU.
void FineSplitPass::linkPiece(
    std::uint32_t sum, std::uint32_t draw, std::uint32_t tag, std::uint32_t bytes, bool resplit) {
  const std::uint32_t prim = frame_.primitive;
  if (resplit) {
    const std::uint32_t cursor = core_.mem_r32(kSplitPrimitiveCursor);
    core_.mem_w32(cursor, prim);
    core_.mem_w32(kSplitPrimitiveCursor, cursor + 4);
  }
  core_.mem_w32(prim, tag);
  frame_.v0 = frame_.linkAndAdvance(polygonBin(sum, draw), bytes);
  frame_.v1 = prim >> 16;
}

// 8002775C: the sixteen GT4 pieces. Each quadrant of the quad has its own texture pair, and each
// piece takes the quarter of it between the pair's corner nearest the piece and the other three.
void FineSplitPass::drawQuadPieces(const SplitPolygon &polygon,
                                   std::uint32_t texture,
                                   std::uint32_t draw) {
  const std::uint32_t quadrants = texture + 0x10;
  for (std::uint32_t k = 0; k < 16; ++k) {
    const std::uint32_t word = core_.mem_r32(kQuadPieces + 4 * k);
    const std::uint32_t quadrant = word & 0xFu;
    const std::uint32_t first = word ^ quadrant;
    const std::array<std::uint32_t, 4> pieceCells = {
        first, first + kCellSize, first + kRowStride, first + kRowStride + kCellSize};
    std::array<std::uint32_t, 4> codes{};
    for (std::size_t i = 0; i < 4; ++i) {
      codes[i] = core_.mem_r32(pieceCells[i] + cell::kDepth);
    }
    const std::uint32_t pair = quadrants + ((quadrant & 0xCu) << 1);
    std::uint32_t uv = core_.mem_r32(pair);
    if ((codes[0] & codes[1] & codes[2] & codes[3] & kOutcodeMask) != 0) {
      continue;
    }
    const std::uint32_t page = core_.mem_r32(pair + 4);
    const std::uint32_t sum = (codes[0] & kDepthMask) + (codes[1] & kDepthMask) +
                              (codes[2] & kDepthMask) + (codes[3] & kDepthMask);
    if (sum == 0) {
      continue;
    }
    const std::uint32_t prim = frame_.primitive;
    core_.mem_w32(prim + packet::kColour0, core_.mem_r32(pieceCells[0] + cell::kColour));
    core_.mem_w32(prim + packet::kColour1, core_.mem_r32(pieceCells[1] + cell::kColour));
    core_.mem_w32(prim + packet::kColour2, core_.mem_r32(pieceCells[2] + cell::kColour));
    core_.mem_w32(prim + packet::kColour3, core_.mem_r32(pieceCells[3] + cell::kColour));

    // 800277F8: the CLUT and page halfwords, then the four corner UVs of the pair (remapped like
    // the coarse pass's) and the piece's UVs halfway from its own corner to each of them.
    core_.mem_w16(prim + packet::kUv0 + 2, static_cast<std::uint16_t>(uv >> 16));
    uv &= 0xFFFFu;
    core_.mem_w16(prim + packet::kUv1 + 2, static_cast<std::uint16_t>(page >> 16));
    std::array<std::uint32_t, 4> corners = {uv, uv + 0x1Fu, uv + 0x1F00u, page & 0xFFFFu};
    const std::uint32_t remap = (page >> 25) & 0x38u;
    if (remap != 0) {
      const std::uint32_t rest = core_.mem_r32(kPageRemaps + remap + 4);
      const std::uint32_t head = core_.mem_r32(kPageRemaps + remap);
      corners[2] = uv + (rest & 0xFFFFu);
      corners[3] = uv + (rest >> 16);
      corners[0] = uv + (head & 0xFFFFu);
      corners[1] += static_cast<std::uint32_t>(asSigned(head) >> 16);
    }
    const std::uint32_t own = corners[quadrant & 3u] & kUvAverageMask;
    const std::array<std::uint32_t, 4> slots = {
        packet::kUv0, packet::kUv1, packet::kUv2, packet::kUv3};
    for (std::size_t i = 0; i < 4; ++i) {
      core_.mem_w16(prim + slots[i],
                    static_cast<std::uint16_t>(((corners[i] & kUvAverageMask) + own) >> 1));
    }

    std::array<std::uint32_t, 4> screens{};
    for (std::size_t i = 0; i < 4; ++i) {
      screens[i] = core_.mem_r32(pieceCells[i] + cell::kScreen);
    }
    if (polygon.overflow &&
        !facesCamera(frame_, screens[0], screens[1], screens[2], screens[3], draw)) {
      core_.mem_w32(prim + packet::kXy0, screens[0]);
      continue;
    }
    core_.mem_w32(prim + packet::kXy0, screens[0]);
    core_.mem_w32(prim + packet::kXy1, screens[1]);
    core_.mem_w32(prim + packet::kXy2, screens[2]);
    core_.mem_w32(prim + packet::kXy3, screens[3]);
    linkPiece(sum, draw, kQuadTag, kQuadBytes, exceedsGpuSpan(screens));
  }
  // The loop's exit test reads one word past the table into v1.
  frame_.v0 = kQuadPieces + 16 * 4;
  frame_.v1 = core_.mem_r32(kQuadPieces + 16 * 4);
}

// 80027B8C.
void FineSplitPass::triangle(const SplitSector &sector, const SplitPolygon &polygon) {
  const PackedIndices vertices(core_.mem_r32(polygon.record + polygon::kIndices));
  const GridPoint a = unpackGridVertex(frame_, sector, vertices.first);
  const GridPoint b = unpackGridVertex(frame_, sector, vertices.second);
  const GridPoint c = unpackGridVertex(frame_, sector, vertices.third);
  const std::uint32_t links = core_.mem_r32(polygon.record + polygon::kLinks);
  const std::uint32_t draw = core_.mem_r32(polygon.record + polygon::kDraw);
  storeComponent(core_, cell::kZ, triangleGrid(a.z, b.z, c.z, coordinateMidpoint));
  storeComponent(core_, cell::kX, triangleGrid(a.x, b.x, c.x, coordinateMidpoint));
  storeComponent(core_, cell::kY, triangleGrid(a.y, b.y, c.y, coordinateMidpoint));

  const std::uint32_t colourWord = core_.mem_r32(polygon.record + polygon::kColours);
  const PackedIndices colourIndices(colourWord);
  const std::uint32_t texture = frame_.textures + (draw & kTextureRecordMask) * kTextureRecordSize;
  const std::uint32_t page = core_.mem_r32(texture + 4);
  const std::uint32_t code = asSigned(page) > 0 ? kOpaqueTriangleCode : kSemiTriangleCode;
  std::array<std::uint32_t, 3> colours = {
      core_.mem_r32(sector.colours + colourIndices.first) + code,
      core_.mem_r32(sector.colours + colourIndices.second) + code,
      core_.mem_r32(sector.colours + colourIndices.third) + code};
  loadCornerColours(frame_, colours.data(), 3);
  storeColours(core_, triangleGrid(colours[0], colours[1], colours[2], colourMidpoint));

  projectGrid(frame_, kTriangleGridCells, true);
  const std::uint32_t uv = core_.mem_r32(texture);
  const std::uint32_t corner = draw & kTriangleCornerMask;
  drawCracks(triangleNeighbourEdges(frame_, links, colourWord, kNeighbourDeferredCoarse),
             kTriangleCracks + (corner >> 6) + (corner >> 9),
             0,
             uv,
             page,
             draw);
  drawTrianglePieces(polygon, texture, draw);
}

// 80028248: the sixteen GT3 pieces, each a sub-texture of the record chosen by the dropped
// corner, shifted by kPieceShift.
void FineSplitPass::drawTrianglePieces(const SplitPolygon &polygon,
                                       std::uint32_t texture,
                                       std::uint32_t draw) {
  const std::uint32_t corner = (draw >> 12) & 3u;
  const std::uint32_t subTextures = texture + 0x10;
  for (std::uint32_t k = 0; k < 16; ++k) {
    const std::uint32_t layout = core_.mem_r32(kTrianglePieces + 4 * k);
    const auto select = static_cast<std::uint32_t>(
        static_cast<std::int8_t>(core_.mem_r8(kPieceTexture + corner + (layout & 0x3Cu))));
    const std::array<std::uint32_t, 3> pieceCells = {kScratchpad + ((layout >> 20) & 0xFF0u),
                                                     kScratchpad + ((layout >> 12) & 0xFF0u),
                                                     kScratchpad + ((layout >> 4) & 0xFF0u)};
    const std::uint32_t pair = (select & 0x18u) + subTextures;
    std::uint32_t page = core_.mem_r32(pair + 4);
    const std::uint32_t shift =
        core_.mem_r32(kPieceShift + ((select & 6u) << 1) + ((page >> 24) & 0x70u));
    std::uint32_t uv = core_.mem_r32(pair);
    page += static_cast<std::uint32_t>(asSigned(shift) >> 16);
    uv += shift & 0xFFFFu;
    const std::uint32_t deltas =
        kTriangleUvs + (((corner + layout) & 3u) << 6) + ((page >> 25) & 0x38u);
    const std::uint32_t delta = core_.mem_r32(deltas);
    const auto third = static_cast<std::int16_t>(core_.mem_r16(deltas + 4));
    const std::uint32_t prim = frame_.primitive;
    core_.mem_w32(prim + packet::kUv0, (delta & 0xFFFFu) + uv);
    core_.mem_w32(prim + packet::kUv1, static_cast<std::uint32_t>(asSigned(delta) >> 16) + page);
    core_.mem_w32(prim + packet::kUv2, static_cast<std::uint32_t>(third) + uv);

    std::array<std::uint32_t, 3> codes{};
    for (std::size_t i = 0; i < 3; ++i) {
      codes[i] = core_.mem_r32(pieceCells[i] + cell::kDepth);
    }
    frame_.v0 = codes[1];
    frame_.v1 = codes[2];
    if ((codes[0] & codes[1] & codes[2] & kOutcodeMask) != 0) {
      continue;
    }
    core_.mem_w32(prim + packet::kColour0, core_.mem_r32(pieceCells[0] + cell::kColour));
    core_.mem_w32(prim + packet::kColour1, core_.mem_r32(pieceCells[1] + cell::kColour));
    core_.mem_w32(prim + packet::kColour2, core_.mem_r32(pieceCells[2] + cell::kColour));
    const std::uint32_t sum =
        (codes[0] & kDepthMask) + (codes[1] & kDepthMask) + 2 * (codes[2] & kDepthMask);
    frame_.v0 = codes[1] & kDepthMask;
    frame_.v1 = codes[2] & kDepthMask;
    if (sum == 0) {
      continue;
    }
    std::array<std::uint32_t, 3> screens{};
    for (std::size_t i = 0; i < 3; ++i) {
      screens[i] = core_.mem_r32(pieceCells[i] + cell::kScreen);
    }
    frame_.v0 = screens[1];
    frame_.v1 = screens[2];
    if (polygon.overflow &&
        !triangleFacesCamera(frame_, screens[0], screens[1], screens[2], draw)) {
      core_.mem_w32(prim + packet::kXy0, screens[0]);
      continue;
    }
    core_.mem_w32(prim + packet::kXy0, screens[0]);
    core_.mem_w32(prim + packet::kXy1, screens[1]);
    core_.mem_w32(prim + packet::kXy2, screens[2]);
    linkPiece(sum, draw, kTriangleTag, kTriangleBytes, exceedsGpuSpan(screens));
  }
}

} // namespace

void splitFinePolygons(TerrainFrame &frame) {
  frame.core.mem_w32(kSplitPrimitiveCursor, frame.scratch + kResplitQueue);
  FineSplitPass quads(frame, false);
  walkSplitList(frame, frame.scratch + kFineSplitList, quads);
  FineSplitPass triangles(frame, true);
  walkSplitList(frame, frame.scratch + kFineSplitList, triangles);
}

} // namespace spyro2::terrain
