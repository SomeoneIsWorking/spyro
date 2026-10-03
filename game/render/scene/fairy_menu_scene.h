// fairy_menu_scene.h — the native owner of the GS_Fairy draw handler 0x8001D718.
//
// The handler is the world (the field arm's producers, no 2D layers), plus — while the fairy's
// dialogue is up — a shaded box, its lit outline, and the page's captions through the shaded-moby
// pass, and the screen border. The recipe (fairy_menu_recipe) is the pure derivation; this layer
// reads the guest's words, submits the producers, and owns the guest-state writes the replaced
// handler used to make.
#pragma once

#include <cstdint>

class Core;

namespace spyro::fairy_menu_scene {

enum class Refusal {
  None,
  ActorChain,
  Particles,
  Cyclorama,
  Environment,
  Page,         // the dialogue page or its selection is outside the guest's own switch
  Text,         // the caption arena cannot hold the page
  ShadedActors, // 0x80022A2C, which draws the captions' glyph mobys
  ScreenBorder  // 0x80018F30
};

// One frame of the handler. `drawAreaX1` is the active draw env's exclusive right edge.
Refusal submit(Core *core, std::int32_t drawAreaX1);

const char *refusalName(Refusal refusal);

} // namespace spyro::fairy_menu_scene
