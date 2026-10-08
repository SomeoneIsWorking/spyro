#pragma once

#include "screen_fade_recipe.h"

class Core;
struct RenderQueue;

namespace spyro::screen_fade {

// Native owner of screen fade producer 0x800190D4.
//
// The target queue is an argument because two callers publish this layer: the logic frame, into the
// live queue, and the 2D overlay's temporal source, into the isolated in-between queue.
bool submit(Core *core, RenderQueue &target, const spyro::screen_fade_recipe::Recipe &recipe);

} // namespace spyro::screen_fade
