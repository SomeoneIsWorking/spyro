#pragma once

#include "field_collectables_recipe.h"

class Core;
struct RenderQueue;

namespace spyro::field_collectables {

// The guest's HUD block, read through ONE lens. The 2D overlay's endpoint capture and this
// producer's own stage must not read the block twice and disagree: they are the same memory read at
// the same instant, and two copies of an address table are how a shipping owner and the oracle that
// checks it end up reading different memory.
spyro::field_collectables_recipe::State read(Core *core);

// Native producer for FIELD collectables/HUD layer 0x80019300.
//
// IT IS THREE STEPS, BECAUSE THE GUEST'S FUNCTION DOES TWO KINDS OF WORK. func_80019300 appends the
// HUD Mobys to the shaded-Moby queue at 0x800720F4 and, in the completed-gem case, writes a wobble
// byte into each glyph it just placed — all of that is GUEST STATE, and the world-shaded sprite
// queue (0x80022A2C) reads it later in the same frame. The rest of the function is the orb/egg
// POLY_FT4s, which is queue emission. So `commit` performs the guest-state half at the guest's own
// position in the FIELD draw order, and `submit` publishes the sprites, which a reconstruction also
// performs — from a recipe it was handed, so an in-between picture writes nothing into the guest's
// shaded queue. `stage` is the two of them in one call, for a caller that has no pre-derived
// recipe.
bool commit(Core *core, const spyro::field_collectables_recipe::Recipe &recipe);

bool stage(Core *core, spyro::field_collectables_recipe::Recipe &recipe);

bool submit(Core *core,
            RenderQueue &target,
            const spyro::field_collectables_recipe::Recipe &recipe);

} // namespace spyro::field_collectables
