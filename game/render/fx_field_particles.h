#pragma once

class Core;

// Native owner for the reached type-0 arm of RasterizeEmitList (0x800573C8). Returns false before
// guest visibility writes or queue submission when any reached record needs an unported arm.
bool spyro_field_particles_submit(Core *core);
