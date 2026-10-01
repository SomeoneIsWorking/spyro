// spyro2_terrain_far.cpp — the far pass of Spyro 2's terrain drawer (SCUS_944.25 80028B14): every
// sector the classification put on kFarList, drawn from its low-detail mesh as untextured
// Gouraud polygons (G3 / G4).
//
// A far sector's low-detail mesh is a packed vertex array, a colour array and 8-byte polygon
// records. Its vertices are projected into the scratchpad as (screen word, SZ) pairs; for a sector
// the classification found touching a screen edge or the near plane, the screen word is SXY << 5
// with outcode bits below it (1 above, 2 below, 4 outside the window horizontally, 8 nearer than
// the sector's near limit), and a sector whose vertices all share one is skipped whole. Each
// polygon is backface-tested unless two-sided, sorted by its mean depth, and dropped past the
// level's far bin. The pass stops when the primitive buffer reaches its end, the one place the
// drawer runs out of room.
//
// It also tracks the deepest ordering-table bin it starts (the mark at kOrderingTableMark), which
// the drawer stores on exit with the primitive cursor.

#include "core.h"
#include "spyro2_gte.h"
#include "spyro2_render_globals.h"
#include "spyro2_terrain_fog.h"
#include "spyro2_terrain_frame.h"
#include "spyro2_terrain_passes.h"
#include "spyro2_terrain_screen.h"

#include <array>

namespace spyro2::terrain {
namespace {

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

std::uint32_t shiftRightArithmetic(std::uint32_t value, std::uint32_t bits) {
  return static_cast<std::uint32_t>(asSigned(value) >> bits);
}

constexpr std::uint32_t kFarDepth = 0x80067400u; // 80028B1C: depth past which nothing is drawn
constexpr std::uint32_t kPrimitiveSpan = 0x1000; // 80028B2C: the buffer ends at scratch + 0x1000

// The far sector header (the near passes read others, spyro2_terrain_mesh.h).
namespace far_sector {
inline constexpr std::uint32_t kOrigin = 0x08;  // z origin high, x origin low
inline constexpr std::uint32_t kOriginY = 0x0E; // halfword
inline constexpr std::uint32_t kLayout = 0x10;  // see FarLayout
inline constexpr std::uint32_t kData = 0x1C;
} // namespace far_sector

// The layout word: vertex count, colour bytes, record bytes, the near limit, and fog exemption.
struct FarLayout {
  explicit FarLayout(std::uint32_t word)
      : vertexCount(word & 0xFFu), colourBytes((word >> 6) & 0x3FCu),
        recordBytes((word >> 13) & 0x7F8u), nearBias(((word >> 15) & 0x600u) - 0x760u),
        fogExempt(asSigned(word) < 0) {}

  std::uint32_t vertexCount;
  std::uint32_t colourBytes;
  std::uint32_t recordBytes;
  std::uint32_t nearBias; // SZ + nearBias < 0: nearer than the low-detail mesh is meant for
  bool fogExempt;
};

constexpr std::uint32_t kRecordSize = 8;
constexpr std::uint32_t kTwoSided = 0x80000000u; // in the record's vertex word
constexpr std::uint32_t kVertexCellSize = 8;     // screen word, SZ
constexpr std::uint32_t kBinBase = 0x30;
constexpr std::uint32_t kBinBiasMask = 0xF;

constexpr std::uint32_t kOutAbove = 1;
constexpr std::uint32_t kOutBelow = 2;
constexpr std::uint32_t kOutHorizontal = 4;
constexpr std::uint32_t kOutNear = 8;
constexpr std::uint32_t kOutcodeMask = 0xF;
constexpr std::uint32_t kOutcodeShift = 5;

constexpr std::uint32_t kG4Tag = 0x08000000u;
constexpr std::uint32_t kG4Code = 0x38000000u;
constexpr std::uint32_t kG4Bytes = 0x24;
constexpr std::uint32_t kG3Tag = 0x06000000u;
constexpr std::uint32_t kG3Code = 0x30000000u;
constexpr std::uint32_t kG3Bytes = 0x1C;

enum class Sector { Drawn, BufferFull };

class FarPass {
public:
  explicit FarPass(TerrainFrame &frame) : frame_(frame), core_(frame.core) {}

  void run();

private:
  Sector drawSector(std::uint32_t listWord);
  bool projectVertices(std::uint32_t data, std::uint32_t sectorClass, const FarLayout &layout);
  void loadVertex(std::uint32_t word);
  Sector drawPolygon(std::uint32_t record, std::uint32_t colours, bool clipped);
  bool link(std::uint32_t bin, std::uint32_t bytes);

  TerrainFrame &frame_;
  Core &core_;
  std::uint32_t lastBin_ = 0;             // s2
  std::uint32_t bufferEnd_ = 0;           // LO
  std::uint32_t mark_ = 0;                // s3
  std::array<std::uint32_t, 3> camera_{}; // s7, t8, t9
  std::array<std::uint32_t, 3> origin_{}; // s4, s5, s6
};

// 80028B14.
void FarPass::run() {
  lastBin_ = (core_.mem_r32(kFarDepth) >> 7) - 1;
  bufferEnd_ = frame_.scratch + kPrimitiveSpan;
  mark_ = core_.mem_r32(render_globals::kOrderingTableMark);
  for (std::uint32_t i = 0; i < camera_.size(); ++i) {
    camera_[i] = core_.mem_r32(render_globals::kCameraPosition + 4 * i) >> 4;
  }
  std::uint32_t list = frame_.scratch + kFarList;
  for (;;) {
    const std::uint32_t word = core_.mem_r32(list);
    list += 4;
    if (word == 0 || drawSector(word) == Sector::BufferFull) {
      break;
    }
  }
  // 80029118: the drawer's exit stores the mark (the primitive cursor is the drawer's).
  core_.mem_w32(render_globals::kOrderingTableMark, mark_);
}

// 80028B80.
Sector FarPass::drawSector(std::uint32_t listWord) {
  const std::uint32_t sectorClass = listWord & 3u;
  const std::uint32_t sector = listWord ^ sectorClass;
  const std::uint32_t originWord = core_.mem_r32(sector + far_sector::kOrigin);
  origin_ = {(originWord >> 16) - camera_[0],
             camera_[1] - (originWord & 0xFFFFu),
             camera_[2] - core_.mem_r16(sector + far_sector::kOriginY)};
  const FarLayout layout(core_.mem_r32(sector + far_sector::kLayout));
  const std::uint32_t data = sector + far_sector::kData;
  if (!projectVertices(data, sectorClass, layout)) {
    return Sector::Drawn;
  }

  // 80028D8C: the colours, fogged when the level fogs and the sector allows it.
  std::uint32_t colours = data + 4 * layout.vertexCount;
  const std::uint32_t records = colours + layout.colourBytes;
  const std::uint32_t fog = gte_read_ctrl(gte::kLight3);
  if (fog != 0 && !layout.fogExempt) {
    frame_.v0 = records + 4;
    frame_.v1 = fogColours(core_, colours, records + 4, fog);
    colours = kFoggedColours;
  }
  const bool clipped = sectorClass != 0;
  const std::uint32_t recordsEnd = records + layout.recordBytes;
  for (std::uint32_t record = records; record != recordsEnd; record += kRecordSize) {
    if (drawPolygon(record, colours, clipped) == Sector::BufferFull) {
      return Sector::BufferFull;
    }
  }
  return Sector::Drawn;
}

// The vertex word's position relative to the camera into the GTE's V0: z in the top 11 bits,
// x and y below it.
void FarPass::loadVertex(std::uint32_t word) {
  const std::uint32_t z = (word >> 21) + origin_[0];
  const std::uint32_t x = origin_[1] - ((word >> 10) & 0x7FFu);
  const std::uint32_t y = origin_[2] - ((word << 1) & 0x7FFu);
  gte_write_data(gte::kVz0, z);
  gte_write_data(gte::kVxy0, x + (y << 16));
}

// 80028BC0: project every vertex into the scratchpad. Like retail, the loop loads one vertex past
// the array into V0. Returns false when every vertex shares an outcode.
bool FarPass::projectVertices(std::uint32_t data,
                              std::uint32_t sectorClass,
                              const FarLayout &layout) {
  const std::uint32_t end = data + 4 * layout.vertexCount + 8;
  std::uint32_t next = data;
  loadVertex(core_.mem_r32(next));
  next += 8;
  std::uint32_t cell = kScratchpad;
  std::uint32_t shared = 0xFFFFFFFFu;
  do {
    gte_op(&core_, gte::kProject);
    loadVertex(core_.mem_r32(next - 4));
    next += 4;
    const std::uint32_t sxy = gte_read_data(gte::kSxy2);
    const std::uint32_t sz = gte_read_data(gte::kSz3);
    frame_.v0 = sxy;
    frame_.v1 = sz;
    std::uint32_t word = sxy;
    if (sectorClass != 0) {
      word = sxy << kOutcodeShift;
      if ((sectorClass & 1u) != 0) {
        if (aboveTop(sxy)) {
          word += kOutAbove;
        }
        if (belowBottom(sxy)) {
          word += kOutBelow;
        }
        if (frame_.bounds.outsideHorizontal(sxy)) {
          word += kOutHorizontal;
        }
      }
      if (asSigned(sz + layout.nearBias) < 0) {
        word += kOutNear;
      }
      shared &= word;
    }
    core_.mem_w32(cell, word);
    core_.mem_w32(cell + 4, sz);
    cell += kVertexCellSize;
  } while (next != end);
  return (sectorClass & 1u) == 0 || (shared & kOutcodeMask) == 0;
}

// 80028E28: one record, a G4 or (third and fourth vertex equal) a G3.
Sector FarPass::drawPolygon(std::uint32_t record, std::uint32_t colours, bool clipped) {
  const std::uint32_t vertices = core_.mem_r32(record);
  const std::uint32_t colourWord = core_.mem_r32(record + 4);
  const std::array<std::uint32_t, 4> cells = {kScratchpad + ((vertices >> 21) & 0x3F8u),
                                              kScratchpad + ((vertices >> 14) & 0x3F8u),
                                              kScratchpad + ((vertices >> 7) & 0x3F8u),
                                              kScratchpad + (vertices & 0x3F8u)};
  const bool triangle = cells[2] == cells[3];
  const std::uint32_t corners = triangle ? 3 : 4;
  std::array<std::uint32_t, 4> screens{};
  std::uint32_t shared = 0xFFFFFFFFu;
  for (std::uint32_t i = 0; i < corners; ++i) {
    screens[i] = core_.mem_r32(cells[i]);
    shared &= screens[i];
  }
  frame_.v0 = screens[1];
  frame_.v1 = screens[2];
  if (clipped) {
    if ((shared & kOutcodeMask) != 0) {
      return Sector::Drawn;
    }
    for (std::uint32_t i = 0; i < corners; ++i) {
      screens[i] = shiftRightArithmetic(screens[i], kOutcodeShift);
    }
    frame_.v0 = screens[1];
    frame_.v1 = screens[2];
  }
  gte_write_data(gte::kSxy0, screens[0]);
  gte_write_data(gte::kSxy1, screens[1]);
  gte_write_data(gte::kSxy2, screens[2]);
  gte_op(&core_, gte::kWinding);
  std::uint32_t depth = 0;
  for (std::uint32_t i = 0; i < corners; ++i) {
    depth += core_.mem_r32(cells[i] + 4);
  }
  const bool twoSided = (vertices & kTwoSided) != 0;
  std::uint32_t bin = 0;
  if (triangle) {
    // 80029048: one-sided triangles facing away are dropped.
    if (!twoSided && asSigned(gte_read_data(gte::kMac0)) <= 0) {
      return Sector::Drawn;
    }
    depth += core_.mem_r32(cells[2] + 4);
    bin = (depth >> 5) + (colourWord & kBinBiasMask) + kBinBase;
  } else {
    // 80028ECC: a quad facing away gets a second test on its other half, (fourth, second, third).
    if (!twoSided) {
      const std::uint32_t area = gte_read_data(gte::kMac0);
      gte_write_data(gte::kSxy0, screens[3]);
      if (asSigned(area) <= 0) {
        gte_op(&core_, gte::kWinding);
        if (asSigned(gte_read_data(gte::kMac0)) >= 0) {
          return Sector::Drawn;
        }
      }
    }
    bin = (depth >> 5) + (colourWord & kBinBiasMask) * 8 + kBinBase;
  }
  if (asSigned(bin - lastBin_) >= 0) {
    return Sector::Drawn;
  }

  constexpr std::array<std::uint32_t, 4> kColourShifts = {23, 16, 9, 2};
  const std::uint32_t prim = frame_.primitive;
  for (std::uint32_t i = 0; i < corners; ++i) {
    std::uint32_t colour = core_.mem_r32(colours + ((colourWord >> kColourShifts[i]) & 0x1FCu));
    if (i == 0) {
      colour += triangle ? kG3Code : kG4Code;
    }
    core_.mem_w32(prim + 4 + 8 * i, colour);
    core_.mem_w32(prim + 8 + 8 * i, screens[i]);
  }
  core_.mem_w32(prim, triangle ? kG3Tag : kG4Tag);
  return link(bin, triangle ? kG3Bytes : kG4Bytes) ? Sector::Drawn : Sector::BufferFull;
}

// 80028F9C: link the packet just written, unless the buffer is already past its end (then retail
// leaves it written but unlinked and the drawer returns). A bin's first packet may move the mark.
bool FarPass::link(std::uint32_t bin, std::uint32_t bytes) {
  if (asSigned(bufferEnd_ - frame_.primitive) < 0) {
    return false;
  }
  const std::uint32_t prim = frame_.primitive;
  const std::uint32_t slot = frame_.orderingTable + bin * 8;
  if (frame_.linkAndAdvance(bin, bytes) == 0 && asSigned(slot - mark_) > 0) {
    mark_ = slot;
  }
  frame_.v0 = prim >> 16;
  return true;
}

} // namespace

void drawFarSectors(TerrainFrame &frame) {
  FarPass(frame).run();
}

} // namespace spyro2::terrain
