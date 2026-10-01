#include "spyro2_terrain_fog.h"

#include "spyro2_gte.h"

namespace spyro2::terrain {

void loadFogColour(Core &core) {
  gte_write_ctrl(gte::kFarRed, core.mem_r32(kFogColour));
  gte_write_ctrl(gte::kFarGreen, core.mem_r32(kFogColour + 4));
  gte_write_ctrl(gte::kFarBlue, core.mem_r32(kFogColour + 8));
}

std::uint32_t fogColours(Core &core, std::uint32_t colours, std::uint32_t end, std::uint32_t fog) {
  loadFogColour(core);
  std::uint32_t from = colours;
  gte_write_data(gte::kIr0, fog);
  gte_write_data(gte::kRgbc, core.mem_r32(from));
  from += 4;
  std::uint32_t to = kFoggedColours;
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

} // namespace spyro2::terrain
