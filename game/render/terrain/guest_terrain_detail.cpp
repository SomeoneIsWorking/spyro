// guest_terrain_detail.cpp — the two near passes of the terrain drawer of this engine family
// (SCUS_944.25 80024534..80025CC0): every sector the classification listed near enough for detail,
// drawn as Gouraud-textured primitives or deferred for subdivision.
//
// The detail pass (80024534) loads the camera rotation and parks the camera position / 4 in the
// light matrix, then walks kDetailList. Each sector is projected (guest_terrain_mesh.h), its two
// colour arrays optionally fogged toward the level's fog colour into 0x800683F0, and each polygon
// is backface-tested with NCLIP and sized by the sum of its vertex depths:
//
//   under 0x2000                 deferred: to the fine split list when a vertex is nearer than
//                                0x140, otherwise to the coarse one;
//   under 0x7C00, or under 0x8800 with some vertex nearer than 0x1F00
//                                one GT4 (0x34 bytes) or GT3 (0x28 bytes) primitive, each vertex
//                                faded from its near colour toward its far one by depth, and the
//                                texture's CLUT row darkened with the brightest vertex;
//   otherwise                    not drawn (the far pass draws the sector's low-detail mesh).
//
// The translucent pass (80025434) does the same for kTranslucentList with one sector-wide colour
// faded toward black, no backface test, semi-transparent primitives, and a 0x6000 cut-off. Both
// leave the two split lists terminated for the subdivision passes.

#include "guest_terrain_passes.h"

#include "core.h"
#include "guest_gte.h"
#include "guest_render_globals.h"
#include "guest_terrain_fog.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_mesh.h"
#include "guest_terrain_polygon.h"

#include <cstddef>

namespace spyro::guest_terrain {
namespace {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

// ── Depth thresholds, on the sum of a polygon's four depths (a triangle counts its third twice) ─
constexpr std::uint32_t kSplitSum = 0x2000;
constexpr std::uint32_t kPlainSum = 0x7C00;
constexpr std::uint32_t kDropSum = 0x8800;
constexpr std::uint32_t kFineVertexDepth = 0x140;
constexpr std::uint32_t kFadeDepth = 0x1F00; // a vertex at or past this is wholly its far colour
constexpr std::uint32_t kFullBright = 0x1000;
constexpr std::uint32_t kBrightest = 0xFFF;
constexpr std::uint32_t kNearRecord = 8; // the record's second word pair: full-bright texture

// What a vertex's depth made of its colour (80024C94): its near colour outright, its far colour
// outright, or a DPCS fade between them by `fade` = 0x1F00 - depth.
enum class Shade {
  Near,
  Far,
  Faded,
};

struct ShadedVertex {
  std::uint32_t colour;
  std::uint32_t fade;
  Shade shade;
};

class DetailPass {
public:
  explicit DetailPass(TerrainFrame &frame)
      : frame_(frame), core_(frame.core), memory_(frame.memory) {}

  void run();

private:
  void drawSector(std::uint32_t listWord);
  void drawQuad(const PackedIndices &vertices);
  void drawTriangle(const PackedIndices &vertices);
  [[nodiscard]] std::uint32_t screenWord(std::uint32_t index) const;
  [[nodiscard]] std::uint32_t depth(std::uint32_t index) const;
  void
  defer(const std::uint32_t *depths, std::size_t count, std::uint32_t sum, std::uint32_t entry);
  ShadedVertex shadeVertex(std::uint32_t colourIndex, std::uint32_t depth);
  void shadeMiddle(const ShadedVertex &vertex);
  void finishPrimitive(std::uint32_t colourIndex,
                       std::uint32_t depth,
                       std::uint32_t colourSlot,
                       std::uint32_t &record,
                       std::uint32_t &darken);

  TerrainFrame &frame_;
  Core &core_;
  TerrainMemory &memory_;
  std::uint32_t list_ = 0;
  std::uint32_t fog_ = 0;
  // The sector being drawn.
  std::uint32_t sectorWord_ = 0;  // t9: the sector address, plus the split headers it has written
  std::uint32_t flagCursor_ = 0;  // s6: one past the polygon's flag byte; sign clear = no clipping
  std::uint32_t nearColours_ = 0; // t7
  std::uint32_t farColours_ = 0;  // s0
  bool overflowOutcodes_ = false; // t7 ^ s1 non-negative: the vertex words carry overflow bits
  std::uint32_t record_ = 0;      // s1: one past the current polygon record
  std::uint32_t brightest_ = 0;   // at: the brightest fade of the primitive so far
};

void DetailPass::run() {
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    gte_write_ctrl(gte::kRotation0 + i, frame_.rotationWord(i));
  }
  gte_write_ctrl(gte::kLight3, core_.mem_r32(frame_.facts.detail.fogLevel));
  frame_.primitive = frame_.primitiveBase();
  frame_.orderingTable = core_.mem_r32(frame_.globals.orderingTable);
  list_ = frame_.scratch + kDetailList;
  frame_.fineSplitCursor = frame_.scratch + kFineSplitList;
  frame_.coarseSplitCursor = frame_.scratch + kCoarseSplitList;
  frame_.textures = core_.mem_r32(frame_.facts.detail.textureTable);
  const std::uint32_t x = frame_.positionWord(0) >> 2;
  frame_.v0 = frame_.positionWord(1) >> 2;
  frame_.v1 = frame_.positionWord(2) >> 2;
  gte_write_ctrl(gte::kLight0, x);
  gte_write_ctrl(gte::kLight1, frame_.v0);
  gte_write_ctrl(gte::kLight2, frame_.v1);
  for (;;) {
    const std::uint32_t word = memory_.r32(list_);
    list_ += 4;
    if (word == 0) {
      return;
    }
    drawSector(word);
  }
}

// 800245D0.
void DetailPass::drawSector(std::uint32_t listWord) {
  const std::uint32_t sectorClass = listWord & kSectorClassMask;
  sectorWord_ = listWord ^ sectorClass;
  const std::uint32_t sector = sectorWord_;
  flagCursor_ = frame_.scratch + (core_.mem_r32(sector + near_sector::kOriginY) & 0xFFFFu);
  const ProjectedSector projected = projectSector(frame_, sector, sectorClass, NearPass::Detail);
  if (!projected.drawn) {
    return;
  }
  if (!projected.clipped) {
    flagCursor_ = (flagCursor_ << 1) >> 1;
  }

  // 80024948: the colour arrays, fogged first when the level fogs and the sector allows it.
  const std::uint32_t layout = core_.mem_r32(sector + near_sector::kLayout);
  const std::uint32_t colours = projected.vertexEnd - 8;
  const std::uint32_t colourBytes = (layout >> 6) & 0x3FCu;
  const std::uint32_t records = colours + 2 * colourBytes;
  fog_ = gte_read_ctrl(gte::kLight3);
  nearColours_ = colours;
  if (fog_ != 0) {
    const std::uint32_t fogFlag = core_.mem_r32(sector + near_sector::kFog);
    const std::uint32_t end = colours + 4 + 2 * colourBytes;
    frame_.v1 = fogFlag;
    frame_.v0 = end;
    if (asSigned(fogFlag) >= 0) {
      frame_.v1 = fogColours(memory_, core_, frame_.facts, colours, end, fog_);
      nearColours_ = frame_.facts.foggedColours;
    }
  }
  farColours_ = nearColours_ + colourBytes;
  overflowOutcodes_ = asSigned(nearColours_ ^ (projected.overflowed ? 0x80000000u : 0u)) >= 0;

  record_ = records;
  const std::uint32_t recordsEnd = ((layout >> 12) & 0xFF0u) + records;
  std::uint32_t indices = core_.mem_r32(record_);
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

std::uint32_t DetailPass::screenWord(std::uint32_t index) const {
  return memory_.r32(kScratchpad + index);
}

std::uint32_t DetailPass::depth(std::uint32_t index) const {
  return memory_.r16(kVertexDepths + (index >> 1));
}

// 80024B90 / 80025080: a polygon too near for one primitive. `entry` is the split-list entry.
void DetailPass::defer(const std::uint32_t *depths,
                       std::size_t count,
                       std::uint32_t sum,
                       std::uint32_t entry) {
  bool fine = false;
  for (std::size_t i = 0; i < count; ++i) {
    if (depths[i] < kFineVertexDepth) {
      fine = true;
      break;
    }
  }
  if (!fine) {
    deferToSplitList(memory_,
                     frame_.coarseSplitCursor,
                     sectorWord_,
                     kCoarseHeaderWritten,
                     sectorWord_ >> 2,
                     entry);
    memory_.w8(flagCursor_ - 1, 2);
    return;
  }
  if (sum == 0) {
    return;
  }
  deferToSplitList(
      memory_, frame_.fineSplitCursor, sectorWord_, kFineHeaderWritten, sectorWord_ >> 2, entry);
}

// 80024C84: the vertex's colour by depth. Its near colour is also the fade's far colour (FC).
ShadedVertex DetailPass::shadeVertex(std::uint32_t colourIndex, std::uint32_t depth) {
  const std::uint32_t nearColour = memory_.r32(nearColours_ + colourIndex);
  const std::uint32_t fade = kFadeDepth - depth;
  if (asSigned(fade - kFullBright) >= 0) {
    return ShadedVertex{nearColour, fade, Shade::Near};
  }
  const std::uint32_t farColour = memory_.r32(farColours_ + colourIndex);
  if (asSigned(fade) <= 0) {
    return ShadedVertex{farColour, fade, Shade::Far};
  }
  gte_write_ctrl(gte::kFarRed, (nearColour << 4) & 0xFF0u);
  gte_write_ctrl(gte::kFarGreen, (nearColour >> 4) & 0xFF0u);
  gte_write_ctrl(gte::kFarBlue, nearColour >> 12);
  gte_write_data(gte::kRgbc, farColour);
  gte_write_data(gte::kIr0, fade);
  gte_op(&core_, gte::kFadeColour);
  return ShadedVertex{gte_read_data(gte::kRgb2), fade, Shade::Faded};
}

// A vertex after the first and before the last: track the brightest fade.
void DetailPass::shadeMiddle(const ShadedVertex &vertex) {
  if (vertex.shade == Shade::Near) {
    brightest_ = kBrightest;
  } else if (vertex.shade == Shade::Faded && asSigned(vertex.fade - brightest_) >= 0) {
    brightest_ = vertex.fade;
  }
}

// 80024DB8 / 80025234: the last vertex, which also picks the texture: a primitive with a
// full-bright vertex uses the record's second (full-bright) pair, and every other one darkens its
// CLUT row by how far its brightest vertex is from full.
void DetailPass::finishPrimitive(std::uint32_t colourIndex,
                                 std::uint32_t depth,
                                 std::uint32_t colourSlot,
                                 std::uint32_t &record,
                                 std::uint32_t &darken) {
  const ShadedVertex vertex = shadeVertex(colourIndex, depth);
  memory_.w8(flagCursor_ - 1, 1);
  if (vertex.shade == Shade::Near) {
    record += kNearRecord;
    darken = 0;
  } else {
    if (vertex.shade == Shade::Faded && asSigned(vertex.fade - brightest_) >= 0) {
      brightest_ = vertex.fade;
    }
    darken = 0xFu - (brightest_ >> 8);
    if (vertex.shade == Shade::Faded && darken == 0) {
      record += kNearRecord;
    }
  }
  memory_.w32(frame_.primitive + colourSlot, vertex.colour);
}

// 80024A28.
void DetailPass::drawQuad(const PackedIndices &vertices) {
  const bool clipped = asSigned(flagCursor_) <= 0;
  flagCursor_ += 1;
  std::uint32_t xy0 = screenWord(vertices.first);
  std::uint32_t xy1 = screenWord(vertices.second);
  std::uint32_t xy2 = screenWord(vertices.third);
  std::uint32_t xy3 = screenWord(vertices.fourth);
  std::uint32_t overflow = 0;
  if (clipped) {
    if ((xy0 & xy1 & xy2 & xy3 & 0xFu) != 0) {
      return;
    }
    if (overflowOutcodes_) {
      overflow = (xy0 | xy1 | xy2 | xy3) & kOverflowOutcode;
    }
    xy0 = static_cast<std::uint32_t>(asSigned(xy0) >> 5);
    xy1 = static_cast<std::uint32_t>(asSigned(xy1) >> 5);
    xy2 = static_cast<std::uint32_t>(asSigned(xy2) >> 5);
    xy3 = static_cast<std::uint32_t>(asSigned(xy3) >> 5);
  }
  const std::uint32_t draw = core_.mem_r32(record_ - polygon::kSize + polygon::kDraw);
  gte_write_data(gte::kSxy0, xy0);
  gte_write_data(gte::kSxy1, xy1);
  gte_write_data(gte::kSxy2, xy3);
  gte_op(&core_, gte::kWinding);
  const bool flipped = (draw & kFlippedWinding) != 0;
  if (overflow == 0) {
    std::uint32_t winding = gte_read_data(gte::kMac0);
    if (flipped) {
      winding = 0u - winding;
    }
    if (asSigned(winding) < 0) {
      // 80024B08: the other diagonal's triangle decides.
      gte_write_data(gte::kSxy0, xy2);
      gte_op(&core_, gte::kWinding);
      if ((draw & kTwoSided) == 0) {
        const std::int32_t second = asSigned(gte_read_data(gte::kMac0));
        if (flipped ? second < 0 : second > 0) {
          frame_.v0 = depth(vertices.second);
          frame_.v1 = depth(vertices.third);
          return;
        }
      }
    }
  }
  const std::uint32_t depths[4] = {
      depth(vertices.first), depth(vertices.second), depth(vertices.third), depth(vertices.fourth)};
  frame_.v0 = depths[1];
  frame_.v1 = depths[2];
  const std::uint32_t sum = depths[0] + depths[1] + depths[2] + depths[3];
  if (sum < kSplitSum) {
    defer(depths, 4, sum, record_ - polygon::kSize + (overflow >> 4));
    return;
  }
  if (sum >= kPlainSum) {
    if (sum >= kDropSum) {
      return;
    }
    if (depths[0] >= kFadeDepth && depths[1] >= kFadeDepth && depths[2] >= kFadeDepth &&
        depths[3] >= kFadeDepth) {
      return;
    }
  }

  // 80024C48: one GT4.
  const std::uint32_t prim = frame_.primitive;
  const PackedIndices colours(core_.mem_r32(record_ - polygon::kSize + polygon::kColours));
  const std::uint32_t bin = polygonBin(sum, draw);
  memory_.w32(prim + packet::kXy0, xy0);
  memory_.w32(prim + packet::kXy1, xy1);
  memory_.w32(prim + packet::kXy2, xy3);
  memory_.w32(prim + packet::kXy3, xy2);
  std::uint32_t record = (draw & kTextureRecordMask) * kTextureRecordSize;

  const ShadedVertex first = shadeVertex(colours.first, depths[0]);
  memory_.w32(prim + packet::kColour0, first.colour | kOpaqueQuadCode);
  brightest_ = first.shade == Shade::Near  ? kBrightest
               : first.shade == Shade::Far ? 0u
                                           : first.fade;
  const ShadedVertex second = shadeVertex(colours.second, depths[1]);
  memory_.w32(prim + packet::kColour1, second.colour);
  shadeMiddle(second);
  const ShadedVertex third = shadeVertex(colours.third, depths[2]);
  memory_.w32(prim + packet::kColour3, third.colour);
  shadeMiddle(third);
  std::uint32_t darken = 0;
  finishPrimitive(colours.fourth, depths[3], packet::kColour2, record, darken);

  record += frame_.textures;
  const std::uint32_t uv = core_.mem_r32(record) + (darken << 22);
  const std::uint32_t page = core_.mem_r32(record + 4);
  memory_.w32(prim, kQuadTag);
  memory_.w32(prim + packet::kUv0, uv);
  if (asSigned(page) < 0) {
    memory_.w8(prim + packet::kCode, static_cast<std::uint8_t>(kSemiQuadCode >> 24));
  }
  memory_.w32(prim + packet::kUv1, page - 0x1F00u);
  memory_.w32(prim + packet::kUv2, uv + 0x1F00u);
  memory_.w16(prim + packet::kUv3, static_cast<std::uint16_t>(page));
  frame_.v1 = uv + 0x1F00u;
  frame_.v0 = prim >> 16;
  frame_.linkAndAdvance(bin, kQuadBytes);
}

// 80024F8C.
void DetailPass::drawTriangle(const PackedIndices &vertices) {
  const bool clipped = asSigned(flagCursor_) <= 0;
  flagCursor_ += 1;
  std::uint32_t xy0 = screenWord(vertices.first);
  std::uint32_t xy1 = screenWord(vertices.second);
  std::uint32_t xy2 = screenWord(vertices.third);
  std::uint32_t overflow = 0;
  if (clipped) {
    if ((xy0 & xy1 & xy2 & 0xFu) != 0) {
      return;
    }
    if (overflowOutcodes_) {
      overflow = (xy0 | xy1 | xy2) & kOverflowOutcode;
    }
    xy0 = static_cast<std::uint32_t>(asSigned(xy0) >> 5);
    xy1 = static_cast<std::uint32_t>(asSigned(xy1) >> 5);
    xy2 = static_cast<std::uint32_t>(asSigned(xy2) >> 5);
  }
  const std::uint32_t draw = core_.mem_r32(record_ - polygon::kSize + polygon::kDraw);
  gte_write_data(gte::kSxy0, xy0);
  gte_write_data(gte::kSxy1, xy1);
  gte_write_data(gte::kSxy2, xy2);
  gte_op(&core_, gte::kWinding);
  if (((draw & kTwoSided) | overflow) == 0) {
    std::uint32_t winding = gte_read_data(gte::kMac0);
    if ((draw & kFlippedWinding) != 0) {
      winding = 0u - winding;
    }
    if (asSigned(winding) < 0) {
      frame_.v0 = kVertexDepths + (vertices.second >> 1);
      frame_.v1 = kVertexDepths + (vertices.third >> 1);
      return;
    }
  }
  const std::uint32_t depths[3] = {
      depth(vertices.first), depth(vertices.second), depth(vertices.third)};
  frame_.v0 = depths[1];
  frame_.v1 = depths[2];
  const std::uint32_t sum = depths[0] + depths[1] + 2 * depths[2];
  if (sum < kSplitSum) {
    defer(depths, 3, sum, (record_ - polygon::kSize + (overflow >> 4)) | kSplitTriangle);
    return;
  }
  if (sum >= kPlainSum) {
    if (sum >= kDropSum) {
      return;
    }
    if (depths[0] >= kFadeDepth && depths[1] >= kFadeDepth && depths[2] >= kFadeDepth) {
      return;
    }
  }

  // 80025130: one GT3.
  const std::uint32_t prim = frame_.primitive;
  const PackedIndices colours(core_.mem_r32(record_ - polygon::kSize + polygon::kColours));
  const std::uint32_t bin = polygonBin(sum, draw);
  memory_.w32(prim + packet::kXy0, xy0);
  memory_.w32(prim + packet::kXy1, xy1);
  memory_.w32(prim + packet::kXy2, xy2);
  std::uint32_t record = (draw & kTextureRecordMask) * kTextureRecordSize;

  const ShadedVertex first = shadeVertex(colours.first, depths[0]);
  memory_.w32(prim + packet::kColour0, first.colour | kOpaqueTriangleCode);
  brightest_ = first.shade == Shade::Near  ? kBrightest
               : first.shade == Shade::Far ? 0u
                                           : first.fade;
  const ShadedVertex second = shadeVertex(colours.second, depths[1]);
  memory_.w32(prim + packet::kColour1, second.colour);
  shadeMiddle(second);
  std::uint32_t darken = 0;
  finishPrimitive(colours.third, depths[2], packet::kColour2, record, darken);

  record += frame_.textures;
  const std::uint32_t uv = core_.mem_r32(record) + (darken << 22);
  const std::uint32_t page = core_.mem_r32(record + 4);
  memory_.w32(prim, kTriangleTag);
  if (asSigned(page) < 0) {
    memory_.w8(prim + packet::kCode, static_cast<std::uint8_t>(kSemiTriangleCode >> 24));
  }
  frame_.v1 = writeTriangleUvs(memory_, prim, uv, page, draw & kTriangleCornerMask);
  frame_.v0 = prim >> 16;
  frame_.linkAndAdvance(bin, kTriangleBytes);
}

} // namespace

void drawDetailSectors(TerrainFrame &frame) {
  DetailPass(frame).run();
}

} // namespace spyro::guest_terrain
