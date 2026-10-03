#include "terrain_packet_sink.h"

#include "core.h" // gpu_replay_guest_packet

namespace spyro::terrain_packet_sink {

// The walk is `forEachPacket` (terrain_packet_sink.h) with the framework's guest-packet funnel as
// its visitor, so the FIFO state machine, the texpage/CLUT/draw-area resolution and the layer
// classification are the same code the guest's own DrawOTag ran — and the order it walks is the
// order that test_terrain_packet_sink proves.
void submit(Core &core, guest_terrain::TerrainMemory &memory, const Bound &bound) {
  forEachPacket(
      memory, bound, [&core](std::uint32_t packet, const std::uint32_t *words, unsigned count) {
        gpu_replay_guest_packet(&core, packet, words, count);
      });
}

} // namespace spyro::terrain_packet_sink
