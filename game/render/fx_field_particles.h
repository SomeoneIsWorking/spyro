#pragma once

#include "producer_refusal.h"

class Core;

namespace spyro::field_particles {

// Native owner for the reached type-0 arm of RasterizeEmitList (0x800573C8). Returns a refusal —
// empty on success — before guest visibility writes or queue submission when any reached record
// needs an unported arm. It carries the REASON, because the render boundary aborts on it and an
// abort that names only a guest address cannot be acted on from a log (see producer_refusal.h).
spyro::ProducerRefusal submit(Core *core);

} // namespace spyro::field_particles
