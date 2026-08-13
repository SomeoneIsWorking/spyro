#pragma once

class Core;

// First ownership slice of guest renderer 0x80023AC4.  It resolves the three
// animation layers into host-side model-space vertices, but deliberately emits
// no faces yet.  False means the live actor/model data was structurally invalid.
bool spyro_paired_actor_decode_pose(Core* c);

// Hermetic checks for the shipping delta codec and /16 frame blend.
int spyro_paired_actor_selftest();
