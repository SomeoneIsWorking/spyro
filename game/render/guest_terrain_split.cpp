#include "guest_terrain_split.h"

#include "core.h"
#include "guest_gte.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_polygon.h"

#include <array>
#include <cstddef>

namespace spyro::guest_terrain {
namespace {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;

constexpr std::int32_t asSigned(std::uint32_t value) {
  return static_cast<std::int32_t>(value);
}

// The two height packings (80025D28): the detail pass's (w << 3) & 0x1FFC and the translucent
// pass's w & 0x3FF, told apart by the header's top bit.
constexpr std::uint32_t kDetailHeightShift = 3;
constexpr std::uint32_t kDetailHeightMask = 0x1FFC;
constexpr std::uint32_t kTranslucentHeightMask = 0x3FF;

constexpr std::uint32_t kAbove = 0x1000;
constexpr std::uint32_t kBelow = 0x2000;
constexpr std::uint32_t kLeft = 0x4000;
constexpr std::uint32_t kRight = 0x8000;

// A grid point as RTPS takes it: 16x scale when every coordinate is within [-0x100, 0xFF].
struct GridVertex {
  std::uint32_t z;
  std::uint32_t xy;
  bool scaled;
};

bool fitsScaled(std::int32_t coordinate) {
  return (((coordinate >> 8) + 1) >> 1) == 0;
}

GridVertex loadGridVertex(Core &core, std::uint32_t address) {
  const std::int32_t z = static_cast<std::int16_t>(core.mem_r16(address + cell::kZ));
  const std::int32_t x = static_cast<std::int16_t>(core.mem_r16(address + cell::kX));
  const std::int32_t y = static_cast<std::int16_t>(core.mem_r16(address + cell::kY));
  const auto word = [](std::int32_t value) {
    return static_cast<std::uint32_t>(value);
  };
  if (fitsScaled(z) && fitsScaled(x) && fitsScaled(y)) {
    return GridVertex{word(z) << 4, (word(x) << 4) + (word(y) << 20), true};
  }
  return GridVertex{word(z), word(x) + (word(y) << 16), false};
}

// 80025D24: a sector header word, and its fog exemption restated in L33.
SplitSector readSplitHeader(TerrainFrame &frame, std::uint32_t header) {
  Core &core = frame.core;
  const std::uint32_t sector = header << 2;
  const std::uint32_t zx = core.mem_r32(sector + 0x08);
  SplitSector result;
  if (asSigned(sector) < 0) {
    result.heightShift = kDetailHeightShift;
    result.heightMask = kDetailHeightMask;
  } else {
    result.heightShift = 0;
    result.heightMask = kTranslucentHeightMask;
  }
  gte_write_ctrl(gte::kLight4, core.mem_r32(sector + 0x10) >> 31);
  const std::uint32_t y = core.mem_r16(sector + 0x0E);
  result.originZ = (zx >> 14) - gte_read_ctrl(gte::kLight0);
  result.originX = gte_read_ctrl(gte::kLight1) - ((zx & 0xFFFFu) << 2);
  const std::uint32_t layout = core.mem_r32(sector + 0x14);
  result.originY = gte_read_ctrl(gte::kLight2) - (y << 2);
  result.vertices = sector + 0x1C + ((layout >> 22) & 0x3FCu);
  result.colours = result.vertices + ((layout << 2) & 0x3FCu);
  frame.v0 = layout;
  frame.v1 = y;
  return result;
}

} // namespace

void walkSplitList(TerrainFrame &frame, std::uint32_t list, SplitPolygonPass &pass) {
  SplitSector sector;
  for (;;) {
    std::uint32_t entry = frame.core.mem_r32(list);
    list += 4;
    if (entry == 0) {
      return;
    }
    if (asSigned(entry) >= 0) {
      sector = readSplitHeader(frame, entry);
      entry = frame.core.mem_r32(list);
      list += 4;
    }
    const std::uint32_t bits = entry & 3u;
    pass.polygon(
        sector,
        SplitPolygon{entry ^ bits, (bits & kSplitOverflow) != 0, (bits & kSplitTriangle) != 0});
  }
}

GridPoint
unpackGridVertex(const TerrainFrame &frame, const SplitSector &sector, std::uint32_t index) {
  const std::uint32_t word = frame.core.mem_r32(sector.vertices + index);
  return GridPoint{((word >> 19) & 0x1FFCu) + sector.originZ,
                   sector.originX - ((word >> 8) & 0x1FFCu),
                   sector.originY - ((word << sector.heightShift) & sector.heightMask)};
}

void storeGridPoint(TerrainFrame &frame, std::uint32_t cellIndex, const GridPoint &point) {
  const std::uint32_t address = kScratchpad + cellIndex * kCellSize;
  frame.core.mem_w16(address + cell::kZ, static_cast<std::uint16_t>(point.z));
  frame.core.mem_w16(address + cell::kX, static_cast<std::uint16_t>(point.x));
  frame.core.mem_w16(address + cell::kY, static_cast<std::uint16_t>(point.y));
}

void loadCornerColours(TerrainFrame &frame, std::uint32_t *colours, std::uint32_t count) {
  const std::uint32_t fog = gte_read_ctrl(gte::kLight3);
  if (fog == 0) {
    return;
  }
  gte_write_data(gte::kIr0, fog);
  gte_write_data(gte::kRgbc, colours[0]);
  if (asSigned(gte_read_ctrl(gte::kLight4)) > 0) {
    return;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    if (i != 0) {
      gte_write_data(gte::kRgbc, colours[i]);
    }
    gte_op(&frame.core, gte::kFadeColour);
    colours[i] = gte_read_data(gte::kRgb2);
  }
}

void projectGrid(TerrainFrame &frame, std::uint32_t count, bool markBehind) {
  Core &core = frame.core;
  const std::uint32_t end = kScratchpad + (count + 1) * kCellSize;
  // Retail zeroes the cell after the last so its one-ahead read of it is harmless.
  core.mem_w32(end - kCellSize, 0);
  GridVertex next = loadGridVertex(core, kScratchpad);
  gte_write_data(gte::kVz0, next.z);
  gte_write_data(gte::kVxy0, next.xy);
  std::uint32_t address = kScratchpad + kCellSize;
  do {
    gte_op(&core, gte::kProject);
    const bool scaled = next.scaled;
    next = loadGridVertex(core, address);
    const std::uint32_t sxy = gte_read_data(gte::kSxy2);
    std::uint32_t code = gte_read_data(gte::kSz3);
    gte_write_data(gte::kVz0, next.z);
    gte_write_data(gte::kVxy0, next.xy);
    address += kCellSize;
    if (scaled) {
      code >>= 4;
    }
    if (markBehind && asSigned(code) <= 0) {
      code |= kOutcodeMask;
    }
    if (aboveTop(sxy)) {
      code |= kAbove;
    }
    if (belowBottom(sxy)) {
      code |= kBelow;
    }
    if (frame.bounds.atOrLeftOfLeft(sxy)) {
      code |= kLeft;
    }
    if (frame.bounds.atOrRightOfRight(sxy)) {
      code |= kRight;
    }
    core.mem_w32(address - 2 * kCellSize + cell::kScreen, sxy);
    core.mem_w32(address - 2 * kCellSize + cell::kDepth, code);
  } while (address != end);
}

void emitCrackTriangle(TerrainFrame &frame,
                       std::uint32_t entry,
                       std::uint32_t colourAdjust,
                       std::uint32_t uv,
                       std::uint32_t page,
                       std::uint32_t draw) {
  Core &core = frame.core;
  const std::uint32_t cells = core.mem_r32(entry);
  const std::uint32_t uvFirst = core.mem_r32(entry + 4);
  const std::uint32_t uvRest = core.mem_r32(entry + 8);
  const std::uint32_t c0 = kScratchpad + (cells >> 16);
  const std::uint32_t c1 = kScratchpad + (cells & 0xFFFFu);
  const std::uint32_t c2 = kScratchpad + (uvFirst >> 16);
  const std::uint32_t d0 = core.mem_r32(c0 + cell::kDepth);
  const std::uint32_t d1 = core.mem_r32(c1 + cell::kDepth);
  const std::uint32_t d2 = core.mem_r32(c2 + cell::kDepth);
  if ((d0 & d1 & d2 & kOutcodeMask) != 0) {
    return;
  }
  const std::uint32_t sum = (d0 & kDepthMask) + (d1 & kDepthMask) + 2 * (d2 & kDepthMask);
  const std::uint32_t prim = frame.primitive;
  core.mem_w32(prim + packet::kXy0, core.mem_r32(c0 + cell::kScreen));
  core.mem_w32(prim + packet::kXy1, core.mem_r32(c1 + cell::kScreen));
  core.mem_w32(prim + packet::kXy2, core.mem_r32(c2 + cell::kScreen));
  core.mem_w32(prim + packet::kColour0, core.mem_r32(c0 + cell::kColour) - colourAdjust);
  core.mem_w32(prim + packet::kColour1, core.mem_r32(c1 + cell::kColour));
  core.mem_w32(prim + packet::kColour2, core.mem_r32(c2 + cell::kColour));
  core.mem_w32(prim, kTriangleTag);
  core.mem_w32(prim + packet::kUv0, (uvFirst & 0xFFFFu) + uv);
  core.mem_w32(prim + packet::kUv1, static_cast<std::uint32_t>(asSigned(uvRest) >> 16) + page);
  core.mem_w32(prim + packet::kUv2, (uvRest & 0xFFFFu) + uv);
  frame.linkAndAdvance(polygonBin(sum, draw), kTriangleBytes);
}

bool exceedsGpuSpan(std::span<const std::uint32_t> screens) {
  constexpr std::int32_t kGpuSpanHigh = 0x200;
  constexpr std::int32_t kGpuSpanWide = 0x400;
  constexpr std::array<std::array<std::size_t, 2>, 5> kPairs = {
      {{0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 3}}};
  const std::size_t pairCount = screens.size() == 4 ? kPairs.size() : 3;
  const auto x = [](std::uint32_t sxy) {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(sxy & 0xFFFFu));
  };
  const auto y = [](std::uint32_t sxy) {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(sxy >> 16));
  };
  const auto exceeds = [](std::int32_t span, std::int32_t limit) {
    return span >= limit || span <= -limit;
  };
  for (std::size_t i = 0; i < pairCount; ++i) {
    if (exceeds(y(screens[kPairs[i][0]]) - y(screens[kPairs[i][1]]), kGpuSpanHigh)) {
      return true;
    }
  }
  for (std::size_t i = 0; i < pairCount; ++i) {
    if (exceeds(x(screens[kPairs[i][0]]) - x(screens[kPairs[i][1]]), kGpuSpanWide)) {
      return true;
    }
  }
  return false;
}

void pullTowardPair(TerrainFrame &frame,
                    std::uint32_t point,
                    std::uint32_t first,
                    std::uint32_t second,
                    std::uint32_t ownWeight,
                    std::uint32_t pairWeight) {
  Core &core = frame.core;
  const auto low = [](std::uint32_t sxy) {
    return static_cast<std::uint32_t>(static_cast<std::int16_t>(sxy & 0xFFFFu));
  };
  const auto high = [](std::uint32_t sxy) {
    return static_cast<std::uint32_t>(asSigned(sxy) >> 16);
  };
  const std::uint32_t own = core.mem_r32(point + cell::kScreen);
  gte_write_data(gte::kIr0, ownWeight);
  gte_write_data(gte::kIr1, low(own));
  gte_write_data(gte::kIr2, high(own));
  const std::uint32_t a = core.mem_r32(first + cell::kScreen);
  const std::uint32_t b = core.mem_r32(second + cell::kScreen);
  gte_op(&core, gte::kScaleIr);
  gte_write_data(gte::kIr0, pairWeight);
  gte_write_data(gte::kIr1, low(a) + low(b));
  gte_write_data(gte::kIr2, high(a) + high(b));
  const std::uint32_t ownX = gte_read_data(gte::kMac1);
  const std::uint32_t ownY = gte_read_data(gte::kMac2);
  gte_op(&core, gte::kScaleIr);
  const std::int32_t x = asSigned(ownX + gte_read_data(gte::kMac1)) >> 8;
  const std::int32_t y = asSigned(ownY + gte_read_data(gte::kMac2)) >> 8;
  core.mem_w32(point + cell::kScreen,
               (static_cast<std::uint32_t>(x) & 0xFFFFu) + (static_cast<std::uint32_t>(y) << 16));
}

namespace {

// NCLIP on SXY0..2, signed toward the camera for the polygon's winding.
std::int32_t winding(TerrainFrame &frame, std::uint32_t draw) {
  gte_op(&frame.core, gte::kWinding);
  std::uint32_t area = gte_read_data(gte::kMac0);
  if ((draw & kFlippedWinding) != 0) {
    area = 0u - area;
  }
  return asSigned(area);
}

} // namespace

bool facesCamera(TerrainFrame &frame,
                 std::uint32_t first,
                 std::uint32_t second,
                 std::uint32_t third,
                 std::uint32_t fourth,
                 std::uint32_t draw) {
  gte_write_data(gte::kSxy0, first);
  gte_write_data(gte::kSxy1, second);
  gte_write_data(gte::kSxy2, third);
  if ((draw & kTwoSided) != 0) {
    gte_op(&frame.core, gte::kWinding);
    return true;
  }
  if (winding(frame, draw) >= 0) {
    return true;
  }
  gte_write_data(gte::kSxy0, fourth);
  return winding(frame, draw) <= 0;
}

bool triangleFacesCamera(TerrainFrame &frame,
                         std::uint32_t first,
                         std::uint32_t second,
                         std::uint32_t third,
                         std::uint32_t draw) {
  gte_write_data(gte::kSxy0, first);
  gte_write_data(gte::kSxy1, second);
  gte_write_data(gte::kSxy2, third);
  if ((draw & kTwoSided) != 0) {
    gte_op(&frame.core, gte::kWinding);
    return true;
  }
  return winding(frame, draw) >= 0;
}

std::uint32_t quadNeighbourEdges(TerrainFrame &frame,
                                 std::uint32_t links,
                                 std::uint32_t draw,
                                 std::uint32_t bit) {
  Core &core = frame.core;
  const std::uint32_t flags = frame.scratch;
  const std::uint32_t first = core.mem_r8(flags + (links >> 19)) & bit;
  const std::uint32_t second = core.mem_r8(flags + ((links >> 6) & 0x1FFFu)) & bit;
  const std::uint32_t third = core.mem_r8(flags + (links & 0x3Fu) + ((draw >> 6) & 0x1FC0u)) & bit;
  const std::uint32_t fourth = core.mem_r8(flags + (draw >> 19)) & bit;
  return first + (second << 8) + (third << 16) + (fourth << 24);
}

std::uint32_t triangleNeighbourEdges(TerrainFrame &frame,
                                     std::uint32_t links,
                                     std::uint32_t colourIndices,
                                     std::uint32_t bit) {
  Core &core = frame.core;
  const std::uint32_t flags = frame.scratch;
  const std::uint32_t first = core.mem_r8(flags + (links >> 19)) & bit;
  const std::uint32_t second = core.mem_r8(flags + ((links >> 6) & 0x1FFFu)) & bit;
  const std::uint32_t third =
      core.mem_r8(flags + ((colourIndices & 0xFEu) << 5) + (links & 0x3Fu)) & bit;
  return first + (second << 8) + (third << 16);
}

} // namespace spyro::guest_terrain
