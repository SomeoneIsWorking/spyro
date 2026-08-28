#pragma once

#include <cstdint>

namespace spyro::screen_border_recipe {

struct Recipe {
  bool visible = false;  // the guest emits degenerate zero-height bars; the native queue skips them
  int32_t barHeight = 0; // the stepped guest state (D_800756C0) — the submitter writes it back
  int32_t topY0 = 0;
  int32_t topY1 = 0;
  int32_t bottomY0 = 0;
  int32_t bottomY1 = 0;
  int32_t x0 = 0;
  int32_t x1 = 0;
};

// GS_Playing calls guest border producer 0x80018F30 when
// (g_ScreenBorderEnabled [0x8007570C] != 0 || D_800756C0 [0x800756C0] != 0). The producer first
// STEPS the bar height — enabled: h += g_DeltaTime when h <= 21, then clamp >= 23 back to 22;
// disabled: h -= g_DeltaTime when h > 0, clamp < 0 to 0 — then emits two opaque black POLY_F4
// bars spanning the authored 512-wide screen: top 0..h, bottom 240-h..240 (external/spyro-1
// src/gamestates/draw.c func_80018F30; both prims push the HUD ordering-table head through
// func_800168DC). `deltaTime` is the guest's own g_DeltaTime [0x800756CC] (2..4, main.c), read
// per call. The native picture widens x to the live render width and keeps the authored
// 0..240 vertical extent, exactly like the screen fade.
Recipe field(uint32_t enabled,
             int32_t barHeight,
             int32_t deltaTime,
             int32_t drawOffsetX,
             int32_t drawOffsetY,
             int32_t renderWidth);

} // namespace spyro::screen_border_recipe
