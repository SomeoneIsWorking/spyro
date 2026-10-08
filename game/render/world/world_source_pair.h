#pragma once

#include "world_source.h"

namespace spyro::world_source_pair {

// Structural/material prerequisite for interior geometry sampling. Camera and
// authored coordinates may move; source residency and scene identity belong to
// the temporal history owner. Selected-LOD availability and active animation
// remain the sampler's responsibility, including equally unavailable dormant LODs.
bool compatible(const world_source::Source &previous,
                const world_source::Source &current,
                const char *&why);

} // namespace spyro::world_source_pair
