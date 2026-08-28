#pragma once

class Core;

// Native producer for FIELD collectables/HUD layer 0x80019300. Returns false
// before mutation when the complete reached recipe cannot be represented.
bool spyro_field_collectables_submit(Core *core);
