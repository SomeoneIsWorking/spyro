#include "terrain_state_producer.h"

#include "frame_state.h"
#include "gp0_primitive_decode.h"
#include "lucent/log.h"
#include "spyro_context.h"

#include <algorithm>
#include <cstdlib>

namespace spyro::guest_terrain {

FieldState captureField(Core &core, const guest_render_globals::Globals &globals) {
  const std::vector<std::uint8_t> &visible = spyro::context(core).terrainVisibility;
  if (visible.size() > kScratchpadBytes) {
    lucent::error("terrstate",
                  "refusing to save a terrain field: the level declares {} sectors and the state "
                  "holds {}",
                  visible.size(),
                  kScratchpadBytes);
    std::abort();
  }
  FieldState state;
  state.camera = readCamera(core, globals);
  state.packetBudget = spyro::context(core).terrainPacketBudget;
  state.sectorCount = static_cast<std::uint32_t>(visible.size());
  std::copy(visible.begin(), visible.end(), state.visible);
  return state;
}

void TerrainStateProducer::render(std::span<const std::byte> from,
                                  std::span<const std::byte> to,
                                  float t,
                                  psx::present::PrimitiveSink &sink) const {
  const auto before = psx::present::stateAs<FieldState>(from);
  const auto after = psx::present::stateAs<FieldState>(to);
  const InBetweenCamera camera =
      cameraBetween(core_, globals_, before.camera, after.camera, static_cast<double>(t));
  const std::vector<std::uint8_t> visible = unionVisibility(
      std::span(before.visible, before.sectorCount), std::span(after.visible, after.sectorCount));
  Rebuild rebuild(core_, facts_, globals_, camera, visible, after.packetBudget);
  rebuild.forEachPacket(
      [&sink](std::uint32_t bin, std::uint32_t packet, std::span<const std::uint32_t> words) {
        const auto primitive = psx::gpu::decodePacketPrimitive(words);
        if (!primitive) {
          lucent::error("terrstate",
                        "the {}-word packet at 0x{:08X} in bin {} is not a polygon",
                        words.size(),
                        packet,
                        bin);
          std::abort();
        }
        sink.emit(psx::present::OtSlot{kTerrainTable, bin}, *primitive);
      });
}

} // namespace spyro::guest_terrain
