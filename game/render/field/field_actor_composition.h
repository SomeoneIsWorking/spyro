#pragma once

#include "producer_refusal.h"

class Core;

// Which of the two calls this composition performs. FIELD authors both; the dragon cutscene
// 0x8001CFDC composes the shaded queue alone in most of its states and only reaches the secondary
// pass in the one state that re-runs the full moby setup, so the pair must be selectable without a
// second copy of the shared shadow-cursor transaction.
struct FieldActorComposition {
  bool secondary = true;
  bool shaded = true;
  // The pause menu's second invocation of the shaded pass in one frame (0x80018880 then
  // 0x80022A2C). It is its own painter object, and it retains nothing for interpolation: the world
  // pass's retained frame is the frame's source and a captions-only frame must not replace it.
  bool captionPass = false;
};

// FIELD's authored secondary-actor and shaded-queue calls share the retail shadow cursor. This
// owner prepares the selected calls, admits their painter objects as one batch, then commits and
// publishes them in source order. The regular actor and visible Spyro owners remain separate
// adjacent layers.
spyro::ProducerRefusal spyro_field_actor_composition_submit(Core *core,
                                                            FieldActorComposition composition = {});
