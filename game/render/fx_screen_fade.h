#pragma once

#include "screen_fade_recipe.h"

class Core;

// Direct native owner of screen fade producer 0x800190D4 for its reached
// cutscene invocation. Returns false before mutation when the recipe is invalid.
bool spyro_screen_fade_submit(Core *core, const spyro::screen_fade_recipe::Recipe &recipe);
