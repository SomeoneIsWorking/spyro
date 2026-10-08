#include "terrain_rebuild.h"

#include "guest_gte.h"
#include "lucent/log.h"
#include "ordering_table.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace spyro::guest_terrain {
namespace {

namespace gte = guest_gte;

// The end of the 2 MB main-RAM window a guest packet address can name.
constexpr std::uint32_t kMainRamEnd = psx::gpu::kKseg0Base + 0x200000u;

// The detail pass sign-clears its flag cursor (guest_terrain_detail.cpp), which leaves it in the
// 64 KB window below the scratch block.
constexpr std::uint32_t kKseg0SignBit = 0x80000000u;
constexpr std::uint32_t kFlagWindowBytes = 0x10000u;

// The DPCS-faded copy of a sector's colours: one buffer, overwritten per sector.
constexpr std::uint32_t kFoggedColourBytes = 0x1000u;

// The far pass names bins past 512 (bin 573 measured on SCUS_944.25), so the table window is
// doubled; a bin beyond that is refused by name.
constexpr std::uint32_t kOrderingWindowBytes = 2 * kOrderingTableBytes;

// One field's terrain allocates at most 55,672 bytes of packets (measured on SCUS_944.25).
constexpr std::uint32_t kMinArenaBytes = 0x10000u;

std::int32_t signedHalf(std::uint32_t word) {
  const std::uint32_t half = word & 0xFFFFu;
  return half >= 0x8000u ? static_cast<std::int32_t>(half) - 0x10000
                         : static_cast<std::int32_t>(half);
}

std::uint32_t lerpWord(std::uint32_t from, std::uint32_t to, double f) {
  const auto a = static_cast<std::int64_t>(static_cast<std::int32_t>(from));
  const auto b = static_cast<std::int64_t>(static_cast<std::int32_t>(to));
  const auto v = a + static_cast<std::int64_t>(std::llround(static_cast<double>(b - a) * f));
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(v));
}

} // namespace

CameraState readCamera(Core &core, const guest_render_globals::Globals &globals) {
  // The first two angles share a word, low half then high half; the third is the next word's low
  // half.
  CameraState state;
  state.angles[0] = signedHalf(core.mem_r32(globals.cameraAngles));
  state.angles[1] = signedHalf(core.mem_r32(globals.cameraAngles) >> 16);
  state.angles[2] = signedHalf(core.mem_r32(globals.cameraAngles + 4));
  for (std::uint32_t i = 0; i < 3; ++i) {
    state.position[i] = core.mem_r32(globals.cameraPosition + 4 * i);
  }
  return state;
}

InBetweenCamera cameraBetween(Core &core,
                              const guest_render_globals::Globals &globals,
                              const CameraState &from,
                              const CameraState &to,
                              double f) {
  const double at = std::clamp(f, 0.0, 1.0);
  const guest_camera::Angles angles =
      guest_camera::interpolate({from.angles[0], from.angles[1], from.angles[2]},
                                {to.angles[0], to.angles[1], to.angles[2]},
                                at);
  const guest_camera::Matrices matrices =
      guest_camera::Builder(core, globals.cameraSineTable, globals.cameraCosineTable).build(angles);
  InBetweenCamera camera{};
  for (std::uint32_t i = 0; i < gte::kRotationWords; ++i) {
    camera.rotation[i] = matrices.projection[i];
    camera.viewRotation[i] = matrices.view[i];
  }
  for (std::uint32_t i = 0; i < 3; ++i) {
    camera.position[i] = lerpWord(from.position[i], to.position[i], at);
  }
  return camera;
}

std::vector<std::uint8_t> unionVisibility(std::span<const std::uint8_t> from,
                                          std::span<const std::uint8_t> to) {
  std::vector<std::uint8_t> out(to.size(), 0);
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(to[i] != 0 || (i < from.size() && from[i] != 0));
  }
  return out;
}

Rebuild::Rebuild(Core &core,
                 const Facts &facts,
                 const guest_render_globals::Globals &globals,
                 const InBetweenCamera &camera,
                 std::span<const std::uint8_t> visibleSectors,
                 std::int32_t packetBudget)
    : core_(core), memory_(core), camera_(camera) {
  // The windows are disjoint: the guest's own cursor can already be past the field's packets.
  const std::uint32_t scratch = core.mem_r32(globals.scratchBaseWord) - facts.scratchListsBelowEnd;
  const std::uint32_t tableBase = core.mem_r32(globals.orderingTable);
  const std::uint32_t frameTop = std::max({scratch + facts.scratchListsBelowEnd,
                                           tableBase + kOrderingWindowBytes,
                                           facts.foggedColours + kFoggedColourBytes});
  const PacketArena arena = packetArenaWindow(frameTop, kMainRamEnd);
  if (arena.bytes < kMinArenaBytes) {
    lucent::error("terrrebuild",
                  "refusing a terrain in-between: the packet arena above this field's own state "
                  "runs from 0x{:08X} for {} bytes, and one field's terrain needs more than {}",
                  arena.base,
                  arena.bytes,
                  kMinArenaBytes);
    std::abort();
  }

  // Every range starts empty: seeding from the guest would hand the traversal the last real
  // field's lists and bins. The scratch block runs to `frameTop` because a split list can grow
  // past retail's block when the camera sits between two fields.
  memory_.mapEmpty(kScratchpad, kScratchpadBytes);
  memory_.mapEmpty(scratch, frameTop - scratch);
  memory_.mapEmpty(scratch & ~kKseg0SignBit, kFlagWindowBytes);
  memory_.mapEmpty(tableBase, kOrderingWindowBytes);
  memory_.mapEmpty(facts.foggedColours, kFoggedColourBytes);
  memory_.mapEmpty(arena.base, arena.bytes);

  // The traversal issues the guest's own GTE instructions; the guest reads them back afterwards.
  GTE_SaveRawState(&gteBefore_);

  for (std::size_t i = 0; i < visibleSectors.size(); ++i) {
    if (visibleSectors[i] != 0) {
      memory_.w8(kScratchpad + static_cast<std::uint32_t>(i), visibleSectors[i]);
      ++visible_;
    }
  }
  drawer_.emplace(
      core, facts, globals, memory_, FrameMode::InBetween, &camera_, arena.base, packetBudget);
  const TerrainFrame &ran = drawer_->run();
  bound_ = terrain_packet_sink::Bound{
      .tableBase = tableBase,
      .arenaBase = arena.base,
      .arenaBytes = arena.bytes,
      .linkedBins = &ran.linkedBins(),
  };
}

Rebuild::~Rebuild() {
  GTE_RestoreRawState(&gteBefore_);
}

} // namespace spyro::guest_terrain
