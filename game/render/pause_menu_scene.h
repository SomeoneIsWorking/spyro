// pause_menu_scene.h — the native owner of the shared GS_PauseMenu / GS_InventoryMenu / GS_OldDragon
// draw handler 0x8001A40C.
//
// The handler is the FIELD arm minus its 2D layers, plus a menu. Its zero path calls, in order,
// 0x800521C0 (moby list build), 0x80019698 (actor pass), 0x800573C8 (particles), 0x80050BD0
// (cyclorama) and 0x8002B9CC (environment) — every one of which already has a native owner in
// game/render, because they are the FIELD arm's own calls. Its non-zero path adds the panel, the
// border and the page's captions, which nothing owned; those live in pause_menu_recipe and here.
//
// What it deliberately does NOT reproduce is the handler's four tiled quads, which are a frozen
// copy of the world it just drew rather than menu art — see pause_menu_recipe.h for the evidence
// and the reason. The world is drawn through its own producers on every menu frame instead.
#pragma once

#include "producer_refusal.h"

#include <cstdint>

class Core;

namespace spyro::pause_menu_scene {

// Which of the handler's calls refused. Reported by name at the render boundary, because an abort
// that names only a guest address cannot be acted on from a log (producer_refusal.h).
enum class Refusal {
  None,
  ActorChain,   // 0x80019698
  Particles,    // 0x800573C8
  Cyclorama,    // 0x80050BD0
  Environment,  // 0x8002B9CC
  Text,         // the caption arena, through the shared HUD text owner
  ShadedActors  // 0x80022A2C, which draws the captions' glyph mobys
};

// One frame of the handler. `drawAreaX1` is the active draw env's right edge, so a border line that
// would fall outside the authored panel is refused against the real clip rather than a literal.
Refusal submit(Core *core, std::int32_t drawAreaX1);

const char *refusalName(Refusal refusal);

} // namespace spyro::pause_menu_scene
