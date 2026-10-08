// fairy_menu_scene.h — the native owner of the GS_Fairy draw handler 0x8001D718.
//
// The handler is the field arm's producers, plus — while the dialogue is up — a shaded box, its
// lit outline and the page's captions, plus the screen border.
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
