// guest_terrain_translucent.cpp — the translucent pass of the terrain drawer of this engine family
// (SCUS_944.25 80025434): see guest_terrain_detail.cpp, whose shape it shares.

#include "core.h"
#include "guest_gte.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_mesh.h"
#include "guest_terrain_passes.h"
#include "guest_terrain_polygon.h"

namespace spyro::guest_terrain {
namespace {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

constexpr std::uint32_t kSplitSum = 0x2000;
constexpr std::uint32_t kDropSum = 0x6000;
constexpr std::uint32_t kFineVertexDepth = 0x140;
constexpr std::uint32_t kFadeStart = 0x1000; // nearer than this keeps the sector colour
constexpr std::uint32_t kFadeOverflow = 0x3000;
constexpr std::uint32_t kFadeFull = 0xFFF;

// 80025918: the fade toward black (FC = 0) of a vertex at `depth`: none until 0x1000, then twice
// the distance past it, with retail's clamp, which tests two bits rather than comparing.
std::uint32_t fadeByDepth(std::uint32_t depth) {
  const std::uint32_t past = depth - kFadeStart;
  const std::uint32_t fade = asSigned(past) >= 0 ? past << 1 : 0u;
  return (fade & kFadeOverflow) != 0 ? kFadeFull : fade;
}

class TranslucentPass {
public:
  explicit TranslucentPass(TerrainFrame &frame) : frame_(frame), core_(frame.core) {}

  void run();

private:
  void drawSector(std::uint32_t listWord);
  void drawQuad(const PackedIndices &vertices);
  void drawTriangle(const PackedIndices &vertices);
  [[nodiscard]] std::uint32_t screenWord(std::uint32_t index) const;
  [[nodiscard]] std::uint32_t depth(std::uint32_t index) const;
  void defer(bool fine, std::uint32_t sum, std::uint32_t entry);
  std::uint32_t fadedColour(std::uint32_t depth);

  TerrainFrame &frame_;
  Core &core_;
  std::uint32_t sectorWord_ = 0; // t9: the sector address, plus the split headers it has written
  bool clipped_ = false;         // s6
  std::uint32_t record_ = 0;     // s1: one past the current polygon record
};

// 80025434.
void TranslucentPass::run() {
  std::uint32_t list = frame_.scratch + kTranslucentList;
  for (;;) {
    const std::uint32_t word = core_.mem_r32(list);
    list += 4;
    if (word == 0) {
      break;
    }
    drawSector(word);
  }
  // 80025CC0: terminate the split lists for the subdivision passes.
  core_.mem_w32(frame_.coarseSplitCursor, 0);
  core_.mem_w32(frame_.fineSplitCursor, 0);
}

// 80025440.
void TranslucentPass::drawSector(std::uint32_t listWord) {
  const std::uint32_t sectorClass = listWord & kSectorClassMask;
  sectorWord_ = listWord ^ sectorClass;
  const std::uint32_t sector = sectorWord_;
  const ProjectedSector projected =
      projectSector(frame_, sector, sectorClass, NearPass::Translucent);
  if (!projected.drawn) {
    return;
  }
  clipped_ = projected.clipped;

  // 80025780: one colour for the whole sector, faded toward black.
  const std::uint32_t layout = core_.mem_r32(sector + near_sector::kLayout);
  const std::uint32_t colours = projected.vertexEnd - 8;
  const std::uint32_t colour = core_.mem_r32(colours);
  record_ = colours + 2 * ((layout >> 6) & 0x3FCu);
  const std::uint32_t recordsEnd = ((layout >> 12) & 0xFF0u) + record_;
  std::uint32_t indices = core_.mem_r32(record_);
  gte_write_ctrl(gte::kFarRed, 0);
  gte_write_ctrl(gte::kFarGreen, 0);
  gte_write_ctrl(gte::kFarBlue, 0);
  gte_write_data(gte::kRgbc, colour);
  while (record_ != recordsEnd) {
    const PackedIndices vertices(indices);
    frame_.v0 = vertices.second;
    frame_.v1 = vertices.third;
    indices = core_.mem_r32(record_ + polygon::kSize);
    record_ += polygon::kSize;
    if (vertices.triangle()) {
      drawTriangle(vertices);
    } else {
      drawQuad(vertices);
    }
  }
}

std::uint32_t TranslucentPass::screenWord(std::uint32_t index) const {
  return core_.mem_r32(kScratchpad + index);
}

std::uint32_t TranslucentPass::depth(std::uint32_t index) const {
  return core_.mem_r16(kVertexDepths + (index >> 1));
}

// 80025884: the split lists, under a header that is the sector address / 4 with its top bit
// cleared.
void TranslucentPass::defer(bool fine, std::uint32_t sum, std::uint32_t entry) {
  const std::uint32_t header = (sectorWord_ << 1) >> 3;
  if (!fine) {
    deferToSplitList(
        core_, frame_.coarseSplitCursor, sectorWord_, kCoarseHeaderWritten, header, entry);
    return;
  }
  if (sum == 0) {
    return;
  }
  deferToSplitList(core_, frame_.fineSplitCursor, sectorWord_, kFineHeaderWritten, header, entry);
}

std::uint32_t TranslucentPass::fadedColour(std::uint32_t depth) {
  gte_write_data(gte::kIr0, fadeByDepth(depth));
  gte_op(&core_, gte::kFadeColour);
  return gte_read_data(gte::kRgb2);
}

// 800257F0.
void TranslucentPass::drawQuad(const PackedIndices &vertices) {
  std::uint32_t xy0 = screenWord(vertices.first);
  std::uint32_t xy1 = screenWord(vertices.second);
  std::uint32_t xy2 = screenWord(vertices.third);
  std::uint32_t xy3 = screenWord(vertices.fourth);
  if (clipped_) {
    if ((xy0 & xy1 & xy2 & xy3 & 0xFu) != 0) {
      return;
    }
    xy0 = static_cast<std::uint32_t>(asSigned(xy0) >> 5);
    xy1 = static_cast<std::uint32_t>(asSigned(xy1) >> 5);
    xy2 = static_cast<std::uint32_t>(asSigned(xy2) >> 5);
    xy3 = static_cast<std::uint32_t>(asSigned(xy3) >> 5);
  }
  const std::uint32_t draw = core_.mem_r32(record_ - polygon::kSize + polygon::kDraw);
  const std::uint32_t d0 = depth(vertices.first);
  const std::uint32_t d1 = depth(vertices.second);
  const std::uint32_t d2 = depth(vertices.third);
  const std::uint32_t d3 = depth(vertices.fourth);
  frame_.v0 = d1;
  frame_.v1 = d2;
  const std::uint32_t sum = d0 + d1 + d2 + d3;
  if (sum < kSplitSum) {
    const bool fine = d0 < kFineVertexDepth || d1 < kFineVertexDepth || d2 < kFineVertexDepth ||
                      d3 < kFineVertexDepth;
    defer(fine, sum, record_ - polygon::kSize);
    return;
  }
  if (sum >= kDropSum) {
    return;
  }

  // 80025908: one semi-transparent GT4. Retail pairs the third vertex's colour with the fourth's
  // position and the fourth's colour with the third's (the detail pass pairs them correctly).
  const std::uint32_t prim = frame_.primitive;
  core_.mem_w32(prim + packet::kXy0, xy0);
  core_.mem_w32(prim + packet::kXy1, xy1);
  core_.mem_w32(prim + packet::kXy2, xy3);
  core_.mem_w32(prim + packet::kXy3, xy2);
  const std::uint32_t c0 = fadedColour(d0);
  const std::uint32_t c1 = fadedColour(d1);
  const std::uint32_t c2 = fadedColour(d2);
  core_.mem_w32(prim + packet::kColour0, c0 | kSemiQuadCode);
  core_.mem_w32(prim + packet::kColour1, c1);
  core_.mem_w32(prim + packet::kColour2, c2);
  const std::uint32_t c3 = fadedColour(d3);
  core_.mem_w32(prim + packet::kColour3, c3);
  const std::uint32_t bin = polygonBin(sum, draw);
  const std::uint32_t record = frame_.textures + (draw & kTextureRecordMask) * kTextureRecordSize;
  const std::uint32_t uv = core_.mem_r32(record);
  const std::uint32_t page = core_.mem_r32(record + 4);
  core_.mem_w32(prim, kQuadTag);
  core_.mem_w32(prim + packet::kUv0, uv);
  core_.mem_w32(prim + packet::kUv1, page - 0x1F00u);
  core_.mem_w32(prim + packet::kUv2, uv + 0x1F00u);
  core_.mem_w16(prim + packet::kUv3, static_cast<std::uint16_t>(page));
  frame_.v1 = uv + 0x1F00u;
  frame_.v0 = prim >> 16;
  frame_.linkAndAdvance(bin, kQuadBytes);
}

// 80025A4C.
void TranslucentPass::drawTriangle(const PackedIndices &vertices) {
  std::uint32_t xy0 = screenWord(vertices.first);
  std::uint32_t xy1 = screenWord(vertices.second);
  std::uint32_t xy2 = screenWord(vertices.third);
  if (clipped_) {
    if ((xy0 & xy1 & xy2 & 0xFu) != 0) {
      return;
    }
    xy0 = static_cast<std::uint32_t>(asSigned(xy0) >> 5);
    xy1 = static_cast<std::uint32_t>(asSigned(xy1) >> 5);
    xy2 = static_cast<std::uint32_t>(asSigned(xy2) >> 5);
  }
  const std::uint32_t draw = core_.mem_r32(record_ - polygon::kSize + polygon::kDraw);
  const std::uint32_t d0 = depth(vertices.first);
  const std::uint32_t d1 = depth(vertices.second);
  const std::uint32_t d2 = depth(vertices.third);
  frame_.v0 = d1;
  frame_.v1 = d2;
  const std::uint32_t sum = d0 + d1 + 2 * d2;
  if (sum < kSplitSum) {
    // 80025AC4: retail's fourth test reads the fourth vertex INDEX, not a depth (the triangle path
    // never loads one), so a triangle whose index is under 0x140 / 4 always splits fine.
    const bool fine = d0 < kFineVertexDepth || d1 < kFineVertexDepth || d2 < kFineVertexDepth ||
                      vertices.fourth < kFineVertexDepth;
    defer(fine, sum, (record_ - polygon::kSize) | kSplitTriangle);
    return;
  }
  if (sum >= kDropSum) {
    return;
  }

  // 80025B50: one semi-transparent GT3.
  const std::uint32_t prim = frame_.primitive;
  core_.mem_w32(prim + packet::kXy0, xy0);
  core_.mem_w32(prim + packet::kXy1, xy1);
  core_.mem_w32(prim + packet::kXy2, xy2);
  const std::uint32_t c0 = fadedColour(d0);
  const std::uint32_t c1 = fadedColour(d1);
  core_.mem_w32(prim + packet::kColour0, c0 | kSemiTriangleCode);
  core_.mem_w32(prim + packet::kColour1, c1);
  const std::uint32_t c2 = fadedColour(d2);
  core_.mem_w32(prim + packet::kColour2, c2);
  const std::uint32_t bin = polygonBin(sum, draw);
  const std::uint32_t record = frame_.textures + (draw & kTextureRecordMask) * kTextureRecordSize;
  const std::uint32_t uv = core_.mem_r32(record);
  const std::uint32_t page = core_.mem_r32(record + 4);
  core_.mem_w32(prim, kTriangleTag);
  frame_.v1 = writeTriangleUvs(core_, prim, uv, page, draw & kTriangleCornerMask);
  frame_.v0 = prim >> 16;
  frame_.linkAndAdvance(bin, kTriangleBytes);
}

} // namespace

void drawTranslucentSectors(TerrainFrame &frame) {
  TranslucentPass(frame).run();
}

} // namespace spyro::guest_terrain
