#include "menu_lighting.h"

#include "core.h"

namespace spyro::menu_lighting {

Lighting read(Core *core) {
  Lighting lighting;
  for (std::size_t i = 0; i < lighting.ramp.size(); ++i) {
    lighting.ramp[i] =
        core->mem_r8(kDirectionRampBase + kDirectionRampOffset + static_cast<std::uint32_t>(i));
  }
  lighting.phase = core->mem_r32(kBorderLightingPhase);
  return lighting;
}

} // namespace spyro::menu_lighting
