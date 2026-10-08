// terrain_rebuild.h — the terrain drawer run again over host memory at a camera between two of the
// guest's, and the packets it links, walked in retail's flatten order.
//
// Both in-between owners use it: Spyro 3's world pass replays the packets through the GPU funnel,
// and the state producer (terrain_state_producer.h) turns them into record primitives. It never
// writes guest memory and runs no guest code; the level's own records are read through `Core`.
#pragma once

#include "core.h"
#include "gte_state.h"
#include "guest_camera_builder.h"
#include "guest_render_globals.h"
#include "guest_terrain_drawer.h"
#include "guest_terrain_facts.h"
#include "guest_terrain_frame.h"
#include "guest_terrain_memory.h"
#include "terrain_packet_sink.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spyro::guest_terrain {

// The packet arena an in-between gets: everything above `frameTop`, the end of the last range the
// traversal owns, to the end of main RAM. The address has to be guest-shaped because a packet's
// chain link is a 24-bit main-RAM offset. `bytes == 0` is the refusal case: the caller must not
// present a truncated picture.
struct PacketArena {
  std::uint32_t base = 0;
  std::uint32_t bytes = 0;
};

[[nodiscard]] inline PacketArena packetArenaWindow(std::uint32_t frameTop,
                                                   std::uint32_t mainRamEnd) {
  const std::uint32_t base = (frameTop + 0xFFFu) & ~0xFFFu;
  if (base >= mainRamEnd) {
    return PacketArena{};
  }
  return PacketArena{base, mainRamEnd - base};
}

// The guest's camera state the drawer's camera is built from: the three angles its builder reads
// (0x8001C2F8) and the world position, each one signed 32-bit fixed-point word.
struct CameraState {
  std::int32_t angles[3] = {};
  std::uint32_t position[3] = {};
};

// The camera the guest's globals describe now.
[[nodiscard]] CameraState readCamera(Core &core, const guest_render_globals::Globals &globals);

// The camera at `f` between two states: angles the short way round and rebuilt into both matrices
// by the guest's own builder, positions as signed 32-bit words that wrap as the guest's do.
[[nodiscard]] InBetweenCamera cameraBetween(Core &core,
                                            const guest_render_globals::Globals &globals,
                                            const CameraState &from,
                                            const CameraState &to,
                                            double f);

// The sectors visible in either field, so none that enters or leaves between them is dropped.
[[nodiscard]] std::vector<std::uint8_t> unionVisibility(std::span<const std::uint8_t> from,
                                                        std::span<const std::uint8_t> to);

// One run of the drawer over host memory. Construction does the whole traversal; the packets are
// then walked with `forEachPacket`. The GTE is handed back as found when this is destroyed.
class Rebuild {
public:
  Rebuild(Core &core,
          const Facts &facts,
          const guest_render_globals::Globals &globals,
          const InBetweenCamera &camera,
          std::span<const std::uint8_t> visibleSectors,
          std::int32_t packetBudget);
  ~Rebuild();
  Rebuild(const Rebuild &) = delete;
  Rebuild &operator=(const Rebuild &) = delete;

  [[nodiscard]] std::uint32_t visibleSectors() const {
    return visible_;
  }

  // Calls `visit(bin, packetAddress, gp0Words)` for every packet, deepest bin first.
  template <typename Visit> void forEachPacket(Visit &&visit) {
    terrain_packet_sink::forEachPacket(memory_, bound_, visit);
  }

private:
  Core &core_;
  HostMemory memory_;
  InBetweenCamera camera_;
  GteRawState gteBefore_;
  std::optional<Drawer> drawer_;
  terrain_packet_sink::Bound bound_;
  std::uint32_t visible_ = 0;
};

} // namespace spyro::guest_terrain
