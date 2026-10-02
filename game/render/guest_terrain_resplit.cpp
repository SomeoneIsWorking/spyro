// guest_terrain_resplit.cpp — the re-split pass of the terrain drawer of this engine family
// (SCUS_944.25 80028504..80028B14): the GPU draws nothing 512 or more dots high or 1024 or more
// wide, so every primitive the fine pass found that large (kResplitQueue) is cut in screen space.
//
// The primitive is already linked into the ordering table. Retail empties it in place (its tag
// length byte becomes 0, a no-op the GPU walks through) and chains the pieces after it, ahead of
// whatever followed it. A GT4 becomes eight GT3s over its corners and edge midpoints, a GT3 four.
// Screen position, colour and texture coordinate are each averaged linearly, which is what makes
// this a fallback: the pieces of a perspective-correct split are the fine pass's job. A piece still
// too large goes back on the queue, which this pass walks until it is empty.

#include "core.h"
#include "guest_gte.h"
#include "guest_render_globals.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_passes.h"
#include "guest_terrain_polygon.h"
#include "guest_terrain_screen.h"
#include "guest_terrain_split.h"

#include <array>
#include <cstddef>
#include <span>

namespace spyro::guest_terrain {
namespace {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;

// The pieces, one word each: three cell offsets in the top three bytes and, in bit 0, "never too
// large" (a piece between edge midpoints of a primitive that was only just too large).
// The two piece tables are this image's addresses (SCUS_944.25 80028974 and 800287A8); their counts
// are the same in every image of this family because they are the shape of the data.
constexpr std::uint32_t kTrianglePieceCount = 7;
constexpr std::uint32_t kQuadPieceCount = 12;
constexpr std::uint32_t kPieceFitsGpu = 1;

constexpr std::uint32_t kQuadLength = 0x0C; // GP0 words after a GT4's tag
constexpr std::uint32_t kTagLengthByte = 3;
constexpr std::uint32_t kGt4ToGt3 = 0x08000000u;

// One point of the cut, a 16-byte scratchpad cell: SXY, colour word, the texture coordinate
// halfword, and in the last byte the outcodes of the screen window.
namespace point {
inline constexpr std::uint32_t kScreen = 0x0;
inline constexpr std::uint32_t kColour = 0x4;
inline constexpr std::uint32_t kUv = 0x8;
inline constexpr std::uint32_t kOutcodes = 0xF;
} // namespace point
constexpr std::uint8_t kOutAbove = 1;
constexpr std::uint8_t kOutBelow = 2;
constexpr std::uint8_t kOutLeft = 4;
constexpr std::uint8_t kOutRight = 8;

struct Midpoint {
  std::uint32_t cell;
  std::size_t first;
  std::size_t second;
};

// A GT4's corners and edge midpoints on its 3x3 grid (800285E8), and a GT3's six points (80028830).
constexpr std::array<std::uint32_t, 4> kQuadCorners = {0x00, 0x20, 0x60, 0x80};
constexpr std::array<Midpoint, 5> kQuadMidpoints = {
    {{0x10, 0, 1}, {0x30, 0, 2}, {0x40, 1, 2}, {0x50, 1, 3}, {0x70, 2, 3}}};
constexpr std::uint32_t kQuadPoints = 9;
constexpr std::array<std::uint32_t, 3> kTriangleCorners = {0x00, 0x20, 0x50};
constexpr std::array<Midpoint, 3> kTriangleMidpoints = {{{0x10, 0, 1}, {0x30, 0, 2}, {0x40, 1, 2}}};
constexpr std::uint32_t kTrianglePoints = 6;

constexpr std::array<std::uint32_t, 4> kPacketScreens = {
    packet::kXy0, packet::kXy1, packet::kXy2, packet::kXy3};
constexpr std::array<std::uint32_t, 4> kPacketColours = {
    packet::kColour0, packet::kColour1, packet::kColour2, packet::kColour3};
constexpr std::array<std::uint32_t, 4> kPacketUvs = {
    packet::kUv0, packet::kUv1, packet::kUv2, packet::kUv3};

std::int32_t lowHalf(std::uint32_t word) {
  return static_cast<std::int16_t>(word & 0xFFFFu);
}

std::int32_t highHalf(std::uint32_t word) {
  return static_cast<std::int32_t>(word) >> 16;
}

// The average of two packed (u, v) coordinates, each byte averaged on its own.
std::uint16_t uvMidpoint(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t v = ((a & 0xFF00u) + (b & 0xFF00u)) >> 1;
  const std::uint32_t u = ((a & 0xFFu) + (b & 0xFFu)) >> 1;
  return static_cast<std::uint16_t>((v & 0xFF00u) | u);
}

class ResplitPass {
public:
  explicit ResplitPass(TerrainFrame &frame) : frame_(frame), core_(frame.core) {}

  void run();

private:
  template <std::size_t Corners, std::size_t Midpoints>
  void loadPoints(std::uint32_t primitive,
                  const std::array<std::uint32_t, Corners> &corners,
                  const std::array<Midpoint, Midpoints> &midpoints,
                  std::uint32_t colourAdjust);
  void classifyPoints(std::uint32_t count);
  void emitPieces(std::uint32_t primitive, std::uint32_t pieces, std::uint32_t count);

  TerrainFrame &frame_;
  Core &core_;
  std::uint32_t queueEnd_ = 0; // sp
  std::uint32_t clut_ = 0;     // t5: the first texture word's CLUT half
  std::uint32_t page_ = 0;     // t6: the second's texture page half
};

// 80028504.
void ResplitPass::run() {
  queueEnd_ = core_.mem_r32(kSplitPrimitiveCursor);
  std::uint32_t queue = frame_.scratch + kResplitQueue;
  while (queue != queueEnd_) {
    const std::uint32_t primitive = core_.mem_r32(queue);
    queue += 4;
    const auto length = static_cast<std::int8_t>(core_.mem_r8(primitive + kTagLengthByte));
    core_.mem_w8(primitive + kTagLengthByte, 0);
    if (length == kQuadLength) {
      loadPoints(primitive, kQuadCorners, kQuadMidpoints, kGt4ToGt3);
      classifyPoints(kQuadPoints);
      emitPieces(primitive, frame_.facts.resplit.quadPieces, kQuadPieceCount);
    } else {
      loadPoints(primitive, kTriangleCorners, kTriangleMidpoints, 0);
      classifyPoints(kTrianglePoints);
      emitPieces(primitive, frame_.facts.resplit.trianglePieces, kTrianglePieceCount);
    }
  }
}

// 80028530 / 800287B8: each corner's screen position, colour (a GT4's recoded for GT3) and texture
// coordinate, and the midpoints' averages of them.
template <std::size_t Corners, std::size_t Midpoints>
void ResplitPass::loadPoints(std::uint32_t primitive,
                             const std::array<std::uint32_t, Corners> &corners,
                             const std::array<Midpoint, Midpoints> &midpoints,
                             std::uint32_t colourAdjust) {
  std::array<std::uint32_t, Corners> screens{};
  std::array<std::uint32_t, Corners> colours{};
  std::array<std::uint32_t, Corners> uvs{};
  for (std::size_t i = 0; i < Corners; ++i) {
    screens[i] = core_.mem_r32(primitive + kPacketScreens[i]);
    colours[i] = core_.mem_r32(primitive + kPacketColours[i]) - colourAdjust;
    uvs[i] = core_.mem_r32(primitive + kPacketUvs[i]);
    const std::uint32_t cell = kScratchpad + corners[i];
    core_.mem_w32(cell + point::kScreen, screens[i]);
    core_.mem_w32(cell + point::kColour, colours[i]);
    core_.mem_w16(cell + point::kUv, static_cast<std::uint16_t>(uvs[i]));
  }
  for (const Midpoint &mid : midpoints) {
    const std::uint32_t cell = kScratchpad + mid.cell;
    const std::uint32_t a = screens[mid.first];
    const std::uint32_t b = screens[mid.second];
    core_.mem_w16(cell + point::kScreen,
                  static_cast<std::uint16_t>((lowHalf(a) + lowHalf(b)) >> 1));
    core_.mem_w16(cell + point::kScreen + 2,
                  static_cast<std::uint16_t>((highHalf(a) + highHalf(b)) >> 1));
    core_.mem_w32(cell + point::kColour, blend(colours[mid.first], colours[mid.second]));
    core_.mem_w16(cell + point::kUv, uvMidpoint(uvs[mid.first], uvs[mid.second]));
  }
  clut_ = uvs[0] & 0xFFFF0000u;
  page_ = uvs[1] & 0xFFFF0000u;
}

// 80028748: the screen window's outcodes of every point, so a piece wholly outside one edge is
// dropped. The horizontal edges are the widescreen window's.
void ResplitPass::classifyPoints(std::uint32_t count) {
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t cell = kScratchpad + i * kCellSize;
    const std::uint32_t sxy = core_.mem_r32(cell + point::kScreen);
    std::uint8_t code = 0;
    if (aboveTop(sxy)) {
      code |= kOutAbove;
    }
    if (belowBottom(sxy)) {
      code |= kOutBelow;
    }
    if (frame_.bounds.atOrLeftOfLeft(sxy)) {
      code |= kOutLeft;
    }
    if (frame_.bounds.atOrRightOfRight(sxy)) {
      code |= kOutRight;
    }
    core_.mem_w8(cell + point::kOutcodes, code);
  }
}

// 80028980: chain the pieces after the emptied primitive, then on to what followed it. When it was
// the last of its ordering-table bin, the bin's last-entry word is found and moved to the last
// piece.
void ResplitPass::emitPieces(std::uint32_t primitive, std::uint32_t pieces, std::uint32_t count) {
  const std::uint32_t next = core_.mem_r32(primitive);
  std::uint32_t tail = primitive;
  for (std::uint32_t k = 0; k < count; ++k) {
    const std::uint32_t piece = core_.mem_r32(pieces + 4 * k);
    const std::array<std::uint32_t, 3> cells = {kScratchpad + (piece >> 24),
                                                kScratchpad + ((piece >> 16) & 0xFFu),
                                                kScratchpad + ((piece >> 8) & 0xFFu)};
    frame_.v0 = cells[1];
    frame_.v1 = cells[2];
    const auto outcodes = [this](std::uint32_t cell) {
      return static_cast<std::int8_t>(core_.mem_r8(cell + point::kOutcodes));
    };
    if ((outcodes(cells[0]) & outcodes(cells[1]) & outcodes(cells[2])) > 0) {
      continue;
    }
    const std::uint32_t prim = frame_.primitive;
    std::array<std::uint32_t, 3> screens{};
    for (std::size_t i = 0; i < cells.size(); ++i) {
      core_.mem_w32(prim + kPacketColours[i], core_.mem_r32(cells[i] + point::kColour));
      screens[i] = core_.mem_r32(cells[i] + point::kScreen);
    }
    core_.mem_w32(prim + packet::kUv0, core_.mem_r16(cells[0] + point::kUv) + clut_);
    core_.mem_w32(prim + packet::kUv1, core_.mem_r16(cells[1] + point::kUv) + page_);
    core_.mem_w32(prim + packet::kUv2, core_.mem_r16(cells[2] + point::kUv));
    if ((piece & kPieceFitsGpu) == 0 && exceedsGpuSpan(screens)) {
      core_.mem_w32(queueEnd_, prim);
      queueEnd_ += 4;
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
      core_.mem_w32(prim + kPacketScreens[i], screens[i]);
    }
    core_.mem_w32(prim, kTriangleTag);
    frame_.linkAfter(tail, prim);
    tail = prim;
    frame_.primitive += kTriangleBytes;
  }
  if (next != 0) {
    frame_.linkAfter(tail, next);
    return;
  }
  // 80028AEC: retail searches from bin 0 for the bin whose last entry is the primitive.
  std::uint32_t bin = core_.mem_r32(frame_.globals.orderingTable);
  while (core_.mem_r32(bin) != primitive) {
    bin += 8;
  }
  frame_.v0 = primitive;
  core_.mem_w32(bin, tail);
}

} // namespace

void resplitOversizedPrimitives(TerrainFrame &frame) {
  ResplitPass(frame).run();
}

} // namespace spyro::guest_terrain
