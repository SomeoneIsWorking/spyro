#include "screen_fade_recipe.h"

namespace spyro::screen_fade_recipe {

// 0x800190D4 as reached from 0x8001E9C8 — generated/shard_4.c plus the matching Rosetta body in
// external/spyro-1/src/gamestates/draw.c. The focused test verifies its mode-2 colour/extent
// transcription; isolated real-disc runtime and visual evidence is C228 / issue 0088.
Recipe cutscene(uint32_t fade, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth) {
  if (fade == 0u || renderWidth <= 0) {
    return {};
  }
  const uint8_t colour = (uint8_t)(fade << 4u);
  return {.visible = true,
          .x0 = drawOffsetX,
          .y0 = drawOffsetY + 8,
          .x1 = drawOffsetX + renderWidth,
          .y1 = drawOffsetY + 232,
          .r = colour,
          .g = colour,
          .b = colour,
          .blendMode = 2u};
}

} // namespace spyro::screen_fade_recipe
