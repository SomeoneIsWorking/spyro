#include "screen_border_recipe.h"

namespace spyro::screen_border_recipe {

namespace {

// The guest's stepping rule, transcribed from func_80018F30's prologue. int32 arithmetic
// throughout: enabled ramps the bars up to the authored 22-pixel hold, disabled ramps them
// back down to zero, one g_DeltaTime step per logic frame in both directions.
int32_t stepped(int32_t height, uint32_t enabled, int32_t deltaTime) {
  if (enabled != 0u) {
    if (height <= 21) {
      height += deltaTime;
    }
    if (height >= 23) {
      height = 22;
    }
  } else {
    if (height > 0) {
      height -= deltaTime;
    }
    if (height < 0) {
      height = 0;
    }
  }
  return height;
}

} // namespace

Recipe field(uint32_t enabled,
             int32_t barHeight,
             int32_t deltaTime,
             int32_t drawOffsetX,
             int32_t drawOffsetY,
             int32_t renderWidth) {
  const int32_t height = stepped(barHeight, enabled, deltaTime);
  Recipe recipe;
  recipe.barHeight = height;
  if (height <= 0 || renderWidth <= 0) {
    return recipe;
  }
  recipe.visible = true;
  recipe.x0 = drawOffsetX;
  recipe.x1 = drawOffsetX + renderWidth;
  recipe.topY0 = drawOffsetY;
  recipe.topY1 = drawOffsetY + height;
  recipe.bottomY0 = drawOffsetY + 240 - height;
  recipe.bottomY1 = drawOffsetY + 240;
  return recipe;
}

} // namespace spyro::screen_border_recipe
