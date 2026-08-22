#pragma once

#include "actor_recipe_capture.h"

#include <cstdint>
#include <span>
#include <vector>

class Core;

namespace spyro::actor_scene_oracle {

enum class Status : uint8_t { Disabled, Captured, Refused };

// Diagnostic only: runs the retail list/cull preparation and reports its durable actor records.
// The shipping producer never calls this and never consumes its guest-renderer output.
Status capture(Core *c, std::vector<actor_recipe_capture::Record> &records);

// Exact acceptance comparison between the independently prepared retail records and the semantic
// scene builder. This has no guest side effects and is useful only after capture() returned
// Captured.
bool compare(std::span<const actor_recipe_capture::Record> retail,
             std::span<const actor_recipe_capture::Record> semantic);

} // namespace spyro::actor_scene_oracle
