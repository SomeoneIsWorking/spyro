#pragma once

class Core;

namespace spyro::field_tracers {

// Native owner for the small tracer primitive producer 0x800189F0. Returns false before guest
// screen-position writes or queue submission if the source tables cannot be represented safely.
bool submit(Core *core);

} // namespace spyro::field_tracers
