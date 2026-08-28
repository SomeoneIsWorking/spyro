#pragma once

class Core;

// FIELD's authored secondary-actor and shaded-queue calls share the retail shadow cursor. This
// owner prepares both calls, admits their painter objects as one batch, then commits and publishes
// them in source order. The regular actor and visible Spyro owners remain separate adjacent layers.
bool spyro_field_actor_composition_submit(Core *core);
