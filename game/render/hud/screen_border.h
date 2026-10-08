#pragma once

#include "screen_border_recipe.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::screen_border {

// Guest screen-border producer 0x80018F30: steps the bar-height state at 0x800756C0 by
// g_DeltaTime (0x800756CC) under the enabled flag at 0x8007570C, then pushes the two black bars
// into the HUD layer.
//
// Two steps because they happen at different times: `stage` is guest STATE and must happen on the
// logic frame, after the composition's gate has read the pre-step value; `submit` is the queue
// emission, which a reconstruction also performs from a recipe rather than from the globals.
screen_border_recipe::Recipe stage(Core *core);

// The guest's own gate, evaluated on the PRE-step state: `if (g_ScreenBorderEnabled || D_800756C0)`
// in GamestateDraw's GS_Playing arm. False means the producer would not have been called at all.
bool armed(Core *core);

bool submit(Core *core, RenderQueue &target, const screen_border_recipe::Recipe &recipe);

} // namespace spyro::screen_border
