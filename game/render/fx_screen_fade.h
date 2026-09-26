#pragma once

#include "screen_fade_recipe.h"

class Core;
struct RenderQueue;

// Direct native owner of screen fade producer 0x800190D4 for its reached
// cutscene invocation. Returns false before mutation when the recipe is invalid.
//
// The TARGET QUEUE is an argument because two callers publish this layer: the logic frame, into the
// live queue, and the 2D overlay's temporal source, into the isolated in-between queue. A submitter
// that reached for `game->rq` itself would put a reconstructed picture in the frame that is already
// composed, which is the one thing the interpolation path must never do.
bool spyro_screen_fade_submit(Core *core,
                              RenderQueue &target,
                              const spyro::screen_fade_recipe::Recipe &recipe);
