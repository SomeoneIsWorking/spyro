#pragma once

#include <cstdint>

namespace spyro::screen_fade_recipe {

struct Recipe {
  bool visible = false;
  int32_t x0 = 0;
  int32_t y0 = 0;
  int32_t x1 = 0;
  int32_t y1 = 0;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t blendMode = 0;
};

// Stage 14 calls guest fade producer 0x800190D4 as
// (mode=2, r=g=b=g_Fade*16). The native product widens the uniform overlay to
// the live render width while retaining the authored 8..232 vertical extent.
Recipe cutscene(uint32_t fade, int32_t drawOffsetX, int32_t drawOffsetY, int32_t renderWidth);

} // namespace spyro::screen_fade_recipe
