#pragma once

class Core;

// Direct native owner of GS_Dragon's renderer 0x8001CFDC (stage 8) — the dragon-rescue cutscene.
// It is a per-g_DragonCutscene.m_State composition over eight branches rather than one producer, so
// the branch itself is derived by dragon_scene_recipe and this owner only applies it. Returns the
// guest address of the layer that refused, 1 when the composition itself could not be derived, or
// 0 when the whole state composed.
unsigned dragon_scene_submit(Core *core, int drawOffsetX, int drawOffsetY, int renderWidth);

// Whether the composition this state will apply draws Spyro's model. The paired-actor ownership
// gate is a per-frame contract checked in drawFrame: it demands exactly one invocation of
// 0x80023AC4 when the scene draws the player and exactly zero when it does not, and it aborts
// bare — with no message — when the count disagrees. So the predicate that arms it has to be the
// same state table this owner walks, not a second guess at which stages have a player in them.
bool spyro_dragon_scene_draws_player(Core *core);
