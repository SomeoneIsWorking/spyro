#pragma once

class Core;

// Cohesive native owner for 0x800208FC + 0x80020F34. The stage-0 scene
// orchestrator remains unwired until every required field layer has an atomic
// owner, but this producer is complete for its explicitly supported corpus.
bool spyro_secondary_actor_submit(Core *core);
