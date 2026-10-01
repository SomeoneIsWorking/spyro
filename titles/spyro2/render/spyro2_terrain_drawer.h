#pragma once

#include <cstdint>

class Core;

namespace spyro2 {

inline constexpr std::uint32_t kTerrainDrawerEntry = 0x80023BB4u;

void registerTerrainDrawerOverride(Core &core);

} // namespace spyro2
