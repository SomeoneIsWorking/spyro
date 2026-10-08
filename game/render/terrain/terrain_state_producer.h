// terrain_state_producer.h — the terrain drawer as a state producer: what one field saves, and the
// render that draws the terrain at any t between two saved fields.
//
// The field's packets are the guest's own. The state is only what the drawer needs to be re-run
// elsewhere: the camera and the sectors the guest's visibility call named. The render runs the
// drawer over host memory (terrain_rebuild.h) and hands each packet it linked to the sink in the
// bin it was linked into.
#pragma once

#include "core.h"
#include "guest_render_globals.h"
#include "guest_terrain_facts.h"
#include "state_producer.h"
#include "terrain_rebuild.h"

#include <cstdint>
#include <span>
#include <type_traits>

namespace spyro::guest_terrain {

// The OT table the bins belong to, as named for `core.otTables`.
inline constexpr std::uint16_t kTerrainTable = 0;

struct FieldState {
  CameraState camera;
  std::int32_t packetBudget = 0;
  std::uint32_t sectorCount = 0;
  std::uint8_t visible[kScratchpadBytes] = {};
};
static_assert(std::is_trivially_copyable_v<FieldState>);

// The field the real drawer just drew: the camera it read, the sectors it was told were visible
// and the packet budget its far pass had. Aborts when the level declares more sectors than the
// state holds.
[[nodiscard]] FieldState captureField(Core &core, const guest_render_globals::Globals &globals);

class TerrainStateProducer final : public psx::present::StateProducer {
public:
  TerrainStateProducer(Core &core, const Facts &facts, const guest_render_globals::Globals &globals)
      : core_(core), facts_(facts), globals_(globals) {}

  void render(std::span<const std::byte> from,
              std::span<const std::byte> to,
              float t,
              psx::present::PrimitiveSink &sink) const override;

private:
  Core &core_;
  const Facts &facts_;
  const guest_render_globals::Globals &globals_;
};

} // namespace spyro::guest_terrain
