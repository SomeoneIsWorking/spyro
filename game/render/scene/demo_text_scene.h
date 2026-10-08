// demo_text_scene — the "DEMO MODE" caption the attract demo draws over its level.
//
// Guest build is at 0x80018908, between the collectables and the actor pass in the stage-0 draw.
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

// What the guest's call at 0x8001F000 would build for this g_DemoMode.
Plan plan(std::uint32_t demoMode);

// False, having written nothing, when the HUD arena or the shaded queue cannot hold the caption.
bool submit(Core *core);

} // namespace spyro::demo_text_scene
