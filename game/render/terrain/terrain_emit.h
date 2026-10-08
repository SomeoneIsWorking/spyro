// The terrain producer's one derive/preflight/publish owner (0x8004EBA8).
//
// WHY IT EXISTS. Four callers reach this layer: the guest-facing entry points, the field's
// cyclorama composition, the level-transition scene, and the temporal source that reconstructs an
// in-between present and owns no guest state at all. What they must agree on is everything between
// a captured corpus and the render queue — the recipe, the submission plan, and the draw area the
// present would have to draw into. A second copy of that in the reconstruction would be a picture
// that differs from the game update's for reasons nobody chose.
#pragma once

#include "actor_stage.h"
#include "terrain_recipe.h"
#include "terrain_submitter.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::terrain_emit {

inline constexpr uint32_t kProducerKey = 0x8004EBA8u;
inline constexpr const char *kProducerName = "terrain:direct";

// Shared with every other producer's emission stage, so one refusal reads the same everywhere.
using Status = actor_stage::Emit;

struct Prepared {
  Status status = Status::ValidEmpty;
  terrain_recipe::Recipe recipe;
  terrain_submitter::Plan submitter;
  psxport::native_projection::ProjectionParams projection{};
};

// Derives the recipe and preflights its submission. `interval` is null on a game update, where
// every object is projected through the update's own view matrix; a reconstruction passes the
// interval its pairing produced.
Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 const terrain_recipe::Input &input,
                 const terrain_recipe::Interval *interval = nullptr);

// Publishes a Ready plan under the producer's scope. A ValidEmpty plan publishes nothing and is not
// a failure: a corpus whose every candidate was rejected still completed. Any other status is a
// programming error at the call site, which must have refused before reaching here.
void publish(Core &core, RenderQueue &queue, const Prepared &prepared);

} // namespace spyro::terrain_emit
