#pragma once

class Core;

// Direct native owner of regular actor renderer 0x8001F798. Returns false before queue mutation
// when the current record corpus contains an unsupported arm or material.
bool spyro_actor_submit(Core *c);
