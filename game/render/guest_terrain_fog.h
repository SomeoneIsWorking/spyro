// guest_terrain_fog.h — the distance fog the terrain drawer of this engine family gives a sector's
// vertex colours: when the level fogs (the fade level parked in the light matrix's L33 is non-zero)
// and the sector is not exempt, every colour word is DPCS-faded toward the level's fog colour into
// one shared buffer before the sector's polygons read them.
//
// The fog colour and that shared buffer are this image's addresses, so they arrive on the frame's
// facts rather than as constants here.
#pragma once

#include "core.h"
#include "guest_terrain_facts.h"

#include <cstdint>

namespace spyro::guest_terrain {

// Load the level's fog colour (facts.fogColour: RFC, GFC, BFC words) into the GTE's far colour.
void loadFogColour(Core &core, const Facts &facts);

// Load the fog colour, then fade the colour words [colours, end - 4) by `fog` into
// facts.foggedColours (SCUS_944.25 800249A0, 80028DB0). Like retail, the loop reads the word at end
// - 4 into RGBC without fading it. Returns one past the last faded word written.
std::uint32_t fogColours(
    Core &core, const Facts &facts, std::uint32_t colours, std::uint32_t end, std::uint32_t fog);

} // namespace spyro::guest_terrain
