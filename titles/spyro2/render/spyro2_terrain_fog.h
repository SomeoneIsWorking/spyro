// spyro2_terrain_fog.h — the distance fog Spyro 2's terrain drawer (SCUS_944.25 0x80023BB4) gives
// a sector's vertex colours: when the level fogs (the fade level parked in the light matrix's L33
// is non-zero) and the sector is not exempt, every colour word is DPCS-faded toward the level's fog
// colour into one shared buffer before the sector's polygons read them.
#pragma once

#include "core.h"

#include <cstdint>

namespace spyro2::terrain {

inline constexpr std::uint32_t kFogColour = 0x80067408u;     // RFC, GFC, BFC words
inline constexpr std::uint32_t kFoggedColours = 0x800683F0u; // the faded copy of a sector's colours

// Load the level's fog colour into the GTE's far colour.
void loadFogColour(Core &core);

// Load the fog colour, then fade the colour words [colours, end - 4) by `fog` into
// kFoggedColours (800249A0, 80028DB0). Like retail, the loop reads the word at end - 4 into RGBC
// without fading it. Returns one past the last faded word written.
std::uint32_t fogColours(Core &core, std::uint32_t colours, std::uint32_t end, std::uint32_t fog);

} // namespace spyro2::terrain
