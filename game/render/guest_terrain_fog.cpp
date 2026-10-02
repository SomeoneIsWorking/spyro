#include "guest_terrain_fog.h"

#include "guest_gte.h"

namespace spyro::guest_terrain {

// The GTE register and command numbers are shared vocabulary (guest_gte.h), not this pass's.
namespace gte = guest_gte;
void loadFogColour(Core &core, const Facts &facts) {
  gte_write_ctrl(gte::kFarRed, core.mem_r32(facts.fogColour));
  gte_write_ctrl(gte::kFarGreen, core.mem_r32(facts.fogColour + 4));
  gte_write_ctrl(gte::kFarBlue, core.mem_r32(facts.fogColour + 8));
}

std::uint32_t fogColours(
    Core &core, const Facts &facts, std::uint32_t colours, std::uint32_t end, std::uint32_t fog) {
  loadFogColour(core, facts);
  std::uint32_t from = colours;
  gte_write_data(gte::kIr0, fog);
  gte_write_data(gte::kRgbc, core.mem_r32(from));
  from += 4;
  std::uint32_t to = facts.foggedColours;
  do {
    gte_op(&core, gte::kFadeColour);
    const std::uint32_t next = core.mem_r32(from);
    from += 4;
    const std::uint32_t faded = gte_read_data(gte::kRgb2);
    gte_write_data(gte::kRgbc, next);
    core.mem_w32(to, faded);
    to += 4;
  } while (from != end);
  return to;
}

} // namespace spyro::guest_terrain
