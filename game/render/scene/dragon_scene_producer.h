#pragma once

#include "producer_refusal.h"

class Core;

namespace spyro::dragon_scene {

// Native owner of GS_Dragon's renderer 0x8001CFDC (stage 8). It is a per-state composition over
// eight branches, derived by dragon_scene_recipe and applied here.
ProducerRefusal submit(Core *core, int drawOffsetX, int drawOffsetY, int renderWidth);

// Whether the composition this state will apply draws Spyro's model. The paired-actor ownership
// gate demands exactly one invocation of 0x80023AC4 when it does and zero when it does not.
bool drawsPlayer(Core *core);

} // namespace spyro::dragon_scene
