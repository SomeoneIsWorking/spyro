// demo_text_scene — the "DEMO MODE" caption the attract demo draws over its level.
//
// WHAT RETAIL DOES. GamestateDraw's stage-0 arm (external/spyro-1 src/gamestates/draw.c:2730) calls
// 0x80018908 between the collectables (0x80019300) and the actor pass (0x80019698), when g_DemoMode
// is nonzero (`lw $v0,0x5714($v0) ; beqz` at 0x8001EFF0, `jal 0x80018908` at 0x8001F000). It builds
// "DEMO MODE" through the proportional caption builder 0x800181AC (`jal` at 0x80018964) with
//
//   position (0xC7, 0xC8, 0x1100) = (199, 200, 4352)   0x80018924-0x80018938
//   spacing  (0x10, 1, 0x1400)    = (16, 1, 5120)      0x80018908-0x80018920
//   space width 0x12 = 18, shade index 2               0x8001895C, 0x8001893C/0x80018968
//
// wobbles each glyph (0x8001898C-0x800189CC) and appends the arena to the shaded-Moby queue through
// 0x80018880 (`jal` at 0x800189D4). The string is at 0x80010AC0.
//
// This owner is the native producer for that layer: the FIELD arm
// (`render::FrameRenderer::renderScene`) replaces the guest's whole stage-0 draw with one producer
// per layer. Like the completed-gem text and the pause and tally captions it reproduces the
// guest's own arena and queue writes through `hud_text`, because the shaded-queue scene reads its
// Mobys from them.
#pragma once

#include "hud_text_builder.h"

#include <cstdint>

struct Core;

namespace spyro::demo_text_scene {

struct Plan {
  bool armed = false; // g_DemoMode != 0 at the 0x8001EFF0 gate
  hud_text::Layout layout;
  std::uint8_t shadeIndex = 0;
};

// Pure: what the guest's call at 0x8001F000 would build for this g_DemoMode.
Plan plan(std::uint32_t demoMode);

// Build and queue the caption when the demo is playing. True when nothing was due or the caption
// was queued; false, having written nothing, when the HUD arena or the shaded queue cannot hold it.
bool submit(Core *core);

} // namespace spyro::demo_text_scene
