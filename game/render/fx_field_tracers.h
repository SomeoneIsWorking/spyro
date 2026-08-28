#pragma once

class Core;

// Native owner for the small tracer primitive producer 0x800189F0. Returns false before guest
// screen-position writes or queue submission if the source tables cannot be represented safely.
bool spyro_field_tracers_submit(Core *core);
