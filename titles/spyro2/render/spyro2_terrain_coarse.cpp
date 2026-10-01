#include "core.h"
#include "spyro2_gte.h"
#include "spyro2_terrain_fog.h"
#include "spyro2_terrain_frame.h"
#include "spyro2_terrain_passes.h"
#include "spyro2_terrain_polygon.h"
#include "spyro2_terrain_split.h"

#include <array>

namespace spyro2::terrain {
namespace {

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

// ── Tables in the executable's data ──────────────────────────────────────────────────────────────
constexpr std::uint32_t kQuadCells = 0x80061F60u;      // 80026438: the four sub-quads' first cells
constexpr std::uint32_t kQuadCracks = 0x80061FB0u;     // 800262F0: one crack triangle per edge
constexpr std::uint32_t kPageRemaps = 0x80062040u;     // 80026564: UV remaps by page bits 28..30
constexpr std::uint32_t kTriangleCracks = 0x800620E0u; // 80026988: 3 per dropped corner
constexpr std::uint32_t kTriangleCells = 0x80062090u;  // 80026AD0: the four sub-triangles' cells
constexpr std::uint32_t kTriangleSubTexture = 0x80062290u; // 80026ADC: signed byte offsets
constexpr std::uint32_t kTriangleUvs = 0x80062360u;        // 80026AE4: UV deltas, 0x40 per corner

constexpr std::uint32_t kCrackEntrySize = 12;
constexpr std::uint32_t kGt4ToGt3 = 0x08000000u; // 0x3C/0x3E -> 0x34/0x36
constexpr std::uint32_t kCentreSplitDepth = 0x700;
constexpr std::uint32_t kCentreSplitSpan = 0x100;
constexpr std::uint32_t kTexelSpan = 0x1F00;

// The 3x3 quad grid (cells 0..8, row-major) and the 6-cell triangle grid (0 1 2 / 3 4 / 5).
constexpr std::uint32_t kQuadGridCells = 9;
constexpr std::uint32_t kTriangleGridCells = 6;

std::uint32_t cellAddress(std::uint32_t index) {
  return kScratchpad + index * kCellSize;
}

// 8002654C: a sub-quad's four UV words from its texture pair. Page bits 28..30 select a remap that
// rotates or mirrors the texture by moving its corners; zero is the identity.
void writeQuadUvs(Core &core, std::uint32_t prim, std::uint32_t uv, std::uint32_t page) {
  std::uint32_t first = uv;
  std::uint32_t second = page - kTexelSpan;
  std::uint32_t third = uv + kTexelSpan;
  std::uint32_t fourth = page;
  const std::uint32_t remap = (second >> 25) & 0x38u;
  if (remap != 0) {
    const std::uint32_t rest = core.mem_r32(kPageRemaps + remap + 4);
    const std::uint32_t head = core.mem_r32(kPageRemaps + remap);
    third = uv + (rest & 0xFFFFu);
    fourth = uv + (rest >> 16);
    first = uv + (head & 0xFFFFu);
    second += static_cast<std::uint32_t>(asSigned(head) >> 16);
  }
  core.mem_w32(prim + packet::kUv0, first);
  core.mem_w32(prim + packet::kUv1, second);
  core.mem_w32(prim + packet::kUv2, third);
  core.mem_w32(prim + packet::kUv3, fourth);
}

class CoarseSplitPass final : public SplitPolygonPass {
public:
  explicit CoarseSplitPass(TerrainFrame &frame) : frame_(frame), core_(frame.core) {}

  void run();
  void polygon(const SplitSector &sector, const SplitPolygon &polygon) override;

private:
  void quad(const SplitSector &sector, const SplitPolygon &polygon);
  void triangle(const SplitSector &sector, const SplitPolygon &polygon);
  void refineCentre();
  void drawCracks(std::uint32_t edges,
                  std::uint32_t table,
                  std::uint32_t colourAdjust,
                  std::uint32_t uv,
                  std::uint32_t page,
                  std::uint32_t draw);
  void drawSubQuads(const SplitPolygon &polygon, std::uint32_t texture, std::uint32_t draw);
  void drawSubTriangles(const SplitPolygon &polygon, std::uint32_t texture, std::uint32_t draw);
  void setCellColour(std::uint32_t index, std::uint32_t colour);

  TerrainFrame &frame_;
  Core &core_;
};

void CoarseSplitPass::run() {
  loadFogColour(core_); // 80025CD0
  walkSplitList(frame_, frame_.scratch + kCoarseSplitList, *this);
}

void CoarseSplitPass::polygon(const SplitSector &sector, const SplitPolygon &polygon) {
  if (polygon.triangle) {
    triangle(sector, polygon);
  } else {
    quad(sector, polygon);
  }
}

void CoarseSplitPass::setCellColour(std::uint32_t index, std::uint32_t colour) {
  core_.mem_w32(cellAddress(index) + cell::kColour, colour);
}

// 80025DC4: a quad cut 2x2. Its corners are cells 0, 2, 6, 8 (first, second, fourth, third).
void CoarseSplitPass::quad(const SplitSector &sector, const SplitPolygon &polygon) {
  const PackedIndices vertices(core_.mem_r32(polygon.record + polygon::kIndices));
  const GridPoint p0 = unpackGridVertex(frame_, sector, vertices.first);
  const GridPoint p1 = unpackGridVertex(frame_, sector, vertices.second);
  const GridPoint p3 = unpackGridVertex(frame_, sector, vertices.fourth);
  const GridPoint p2 = unpackGridVertex(frame_, sector, vertices.third);
  const std::uint32_t links = core_.mem_r32(polygon.record + polygon::kLinks);
  const std::uint32_t draw = core_.mem_r32(polygon.record + polygon::kDraw);
  storeGridPoint(frame_, 0, p0);
  storeGridPoint(frame_, 2, p1);
  storeGridPoint(frame_, 6, p3);
  storeGridPoint(frame_, 8, p2);
  storeGridPoint(frame_, 1, midpoint(p0, p1));
  storeGridPoint(frame_, 7, midpoint(p3, p2));
  const GridPoint left = midpoint(p0, p3);
  const GridPoint right = midpoint(p1, p2);
  storeGridPoint(frame_, 3, left);
  storeGridPoint(frame_, 5, right);
  storeGridPoint(frame_, 4, midpoint(left, right));

  // 80025F80: corner colours, fogged, then blended to the edge midpoints and the centre (which
  // blends the second and fourth corners, a diagonal, where its position averages all four).
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
  setCellColour(0, colours[0]);
  setCellColour(2, colours[1]);
  setCellColour(6, colours[2]);
  setCellColour(8, colours[3]);
  setCellColour(1, blend(colours[0], colours[1]));
  setCellColour(7, blend(colours[2], colours[3]));
  setCellColour(3, blend(colours[0], colours[2]));
  setCellColour(5, blend(colours[1], colours[3]));
  setCellColour(4, blend(colours[1], colours[2]));

  projectGrid(frame_, kQuadGridCells, false);
  refineCentre();
  const std::uint32_t uv = core_.mem_r32(texture);
  drawCracks(quadNeighbourEdges(frame_, links, draw, 1), kQuadCracks, kGt4ToGt3, uv, page, draw);
  drawSubQuads(polygon, texture, draw);
}

// 80026200: a near-enough centre is pulled toward the midpoint of the diagonal it was blended
// from, by (0x800 - depth) / 0x100 against (depth - 0x700) / 0x200, so the projected centre of a
// large near polygon does not bulge out of its plane.
void CoarseSplitPass::refineCentre() {
  const std::uint32_t centre = cellAddress(4);
  const std::uint32_t code = core_.mem_r32(centre + cell::kDepth);
  if ((code & kOutcodeMask) != 0) {
    return;
  }
  const std::uint32_t past = (code & kDepthMask) - kCentreSplitDepth;
  if (asSigned(past) <= 0) {
    return;
  }
  pullTowardPair(
      frame_, centre, cellAddress(2), cellAddress(6), kCentreSplitSpan - past, past >> 1);
}

// 80026324: one crack triangle per edge whose neighbour byte is set, from consecutive 12-byte
// table entries.
void CoarseSplitPass::drawCracks(std::uint32_t edges,
                                 std::uint32_t table,
                                 std::uint32_t colourAdjust,
                                 std::uint32_t uv,
                                 std::uint32_t page,
                                 std::uint32_t draw) {
  std::uint32_t entry = table;
  while (edges != 0) {
    const std::uint32_t edge = edges & 0xFFu;
    edges >>= 8;
    entry += kCrackEntrySize;
    if (edge != 0) {
      emitCrackTriangle(frame_, entry - kCrackEntrySize, colourAdjust, uv, page, draw);
    }
  }
}

// 80026438: the four GT4s. Each takes its texture from the record's next word pair, remapped
// through kPageRemaps when the page word's bits 28..30 ask for it.
void CoarseSplitPass::drawSubQuads(const SplitPolygon &polygon,
                                   std::uint32_t texture,
                                   std::uint32_t draw) {
  for (std::uint32_t k = 0; k < 4; ++k) {
    const std::uint32_t first = core_.mem_r32(kQuadCells + 4 * k);
    frame_.v1 = first;
    const std::uint32_t pair = texture + 8 * k + 0x10;
    const std::uint32_t c0 = core_.mem_r32(first + cell::kDepth);
    const std::uint32_t c1 = core_.mem_r32(first + kCellSize + cell::kDepth);
    const std::uint32_t c3 = core_.mem_r32(first + 3 * kCellSize + cell::kDepth);
    const std::uint32_t c4 = core_.mem_r32(first + 4 * kCellSize + cell::kDepth);
    const std::uint32_t uv = core_.mem_r32(pair);
    if ((c0 & c1 & c3 & c4 & kOutcodeMask) != 0) {
      continue;
    }
    const std::uint32_t page = core_.mem_r32(pair + 4);
    const std::uint32_t sum =
        (c0 & kDepthMask) + (c1 & kDepthMask) + (c3 & kDepthMask) + (c4 & kDepthMask);
    const std::uint32_t xy0 = core_.mem_r32(first + cell::kScreen);
    const std::uint32_t xy1 = core_.mem_r32(first + kCellSize + cell::kScreen);
    const std::uint32_t xy3 = core_.mem_r32(first + 3 * kCellSize + cell::kScreen);
    const std::uint32_t xy4 = core_.mem_r32(first + 4 * kCellSize + cell::kScreen);
    const std::uint32_t prim = frame_.primitive;
    if (polygon.overflow && !facesCamera(frame_, xy0, xy1, xy3, xy4, draw)) {
      core_.mem_w32(prim + packet::kXy0, xy0);
      continue;
    }
    core_.mem_w32(prim + packet::kXy0, xy0);
    core_.mem_w32(prim + packet::kXy1, xy1);
    core_.mem_w32(prim + packet::kXy2, xy3);
    core_.mem_w32(prim + packet::kXy3, xy4);
    core_.mem_w32(prim + packet::kColour0, core_.mem_r32(first + cell::kColour));
    core_.mem_w32(prim + packet::kColour1, core_.mem_r32(first + kCellSize + cell::kColour));
    core_.mem_w32(prim + packet::kColour2, core_.mem_r32(first + 3 * kCellSize + cell::kColour));
    core_.mem_w32(prim + packet::kColour3, core_.mem_r32(first + 4 * kCellSize + cell::kColour));
    writeQuadUvs(core_, prim, uv, page);
    core_.mem_w32(prim, kQuadTag);
    frame_.v1 = frame_.linkAndAdvance(polygonBin(sum, draw), kQuadBytes);
  }
  frame_.v0 = kQuadCells + 0x10;
}

// 80026AD0: the four GT3s, each a corner-dependent sub-texture of the record.
void CoarseSplitPass::drawSubTriangles(const SplitPolygon &polygon,
                                       std::uint32_t texture,
                                       std::uint32_t draw) {
  const std::uint32_t corner = (draw & kTriangleCornerMask) >> 12;
  const std::uint32_t subTextures = texture + 0x10;
  for (std::uint32_t k = 0; k < 4; ++k) {
    const std::uint32_t layout = core_.mem_r32(kTriangleCells + 4 * k);
    const auto offset =
        static_cast<std::int8_t>(core_.mem_r8(kTriangleSubTexture + (layout & 0xCu) + corner));
    const std::uint32_t c0 = kScratchpad + ((layout >> 20) & 0xFF0u);
    const std::uint32_t c1 = kScratchpad + ((layout >> 12) & 0xFF0u);
    const std::uint32_t c2 = kScratchpad + ((layout >> 4) & 0xFF0u);
    const std::uint32_t pair = subTextures + static_cast<std::uint32_t>(offset);
    const std::uint32_t page = core_.mem_r32(pair + 4) - kTexelSpan;
    const std::uint32_t uv = core_.mem_r32(pair);
    const std::uint32_t deltas =
        kTriangleUvs + (((layout + corner) & 3u) << 6) + ((page >> 25) & 0x38u);
    const std::uint32_t delta = core_.mem_r32(deltas);
    const auto third = static_cast<std::int16_t>(core_.mem_r16(deltas + 4));
    const std::uint32_t prim = frame_.primitive;
    core_.mem_w32(prim + packet::kUv0, (delta & 0xFFFFu) + uv);
    core_.mem_w32(prim + packet::kUv1, static_cast<std::uint32_t>(asSigned(delta) >> 16) + page);
    core_.mem_w32(prim + packet::kUv2, static_cast<std::uint32_t>(third) + uv);
    const std::uint32_t d0 = core_.mem_r32(c0 + cell::kDepth);
    const std::uint32_t d1 = core_.mem_r32(c1 + cell::kDepth);
    const std::uint32_t d2 = core_.mem_r32(c2 + cell::kDepth);
    frame_.v0 = d1;
    frame_.v1 = d2;
    if ((d0 & d1 & d2 & kOutcodeMask) != 0) {
      continue;
    }
    const std::uint32_t sum = (d0 & kDepthMask) + (d1 & kDepthMask) + 2 * (d2 & kDepthMask);
    const std::uint32_t xy0 = core_.mem_r32(c0 + cell::kScreen);
    const std::uint32_t xy1 = core_.mem_r32(c1 + cell::kScreen);
    const std::uint32_t xy2 = core_.mem_r32(c2 + cell::kScreen);
    frame_.v0 = xy1;
    frame_.v1 = xy2;
    if (polygon.overflow && !triangleFacesCamera(frame_, xy0, xy1, xy2, draw)) {
      core_.mem_w32(prim + packet::kXy0, xy0);
      continue;
    }
    core_.mem_w32(prim + packet::kXy0, xy0);
    core_.mem_w32(prim + packet::kXy1, xy1);
    core_.mem_w32(prim + packet::kXy2, xy2);
    core_.mem_w32(prim + packet::kColour0, core_.mem_r32(c0 + cell::kColour));
    core_.mem_w32(prim + packet::kColour1, core_.mem_r32(c1 + cell::kColour));
    core_.mem_w32(prim + packet::kColour2, core_.mem_r32(c2 + cell::kColour));
    core_.mem_w32(prim, kTriangleTag);
    frame_.v0 = frame_.linkAndAdvance(polygonBin(sum, draw), kTriangleBytes);
    frame_.v1 = prim >> 16;
  }
}

// 800265E4: a triangle cut into four. Its corners are cells 0, 2, 5.
void CoarseSplitPass::triangle(const SplitSector &sector, const SplitPolygon &polygon) {
  const PackedIndices vertices(core_.mem_r32(polygon.record + polygon::kIndices));
  const GridPoint p0 = unpackGridVertex(frame_, sector, vertices.first);
  const GridPoint p1 = unpackGridVertex(frame_, sector, vertices.second);
  const GridPoint p2 = unpackGridVertex(frame_, sector, vertices.third);
  const std::uint32_t links = core_.mem_r32(polygon.record + polygon::kLinks);
  const std::uint32_t draw = core_.mem_r32(polygon.record + polygon::kDraw);
  storeGridPoint(frame_, 0, p0);
  storeGridPoint(frame_, 2, p1);
  storeGridPoint(frame_, 5, p2);
  storeGridPoint(frame_, 1, midpoint(p0, p1));
  storeGridPoint(frame_, 3, midpoint(p0, p2));
  storeGridPoint(frame_, 4, midpoint(p1, p2));

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
  setCellColour(0, colours[0]);
  setCellColour(2, colours[1]);
  setCellColour(5, colours[2]);
  setCellColour(1, blend(colours[0], colours[1]));
  setCellColour(3, blend(colours[0], colours[2]));
  setCellColour(4, blend(colours[1], colours[2]));

  projectGrid(frame_, kTriangleGridCells, false);
  const std::uint32_t uv = core_.mem_r32(texture);
  const std::uint32_t corner = draw & kTriangleCornerMask;
  drawCracks(triangleNeighbourEdges(frame_, links, colourWord, 1),
             kTriangleCracks + (corner >> 7) + (corner >> 10),
             0,
             uv,
             page,
             draw);
  drawSubTriangles(polygon, texture, draw);
}

} // namespace

void splitCoarsePolygons(TerrainFrame &frame) {
  CoarseSplitPass(frame).run();
}

} // namespace spyro2::terrain
