// The world-shaded sprite queue's one derive/preflight/publish owner (0x80022A2C).
//
// WHY IT EXISTS. Three callers reach this layer: the standalone producer, the field composition
// that draws it beside the secondary-actor layer and owns the guest shadow-list transaction the two
// share, and the temporal source that reconstructs an in-between present and owns no guest state at
// all. What they must agree on is everything between a scene input and the queue — the recipe, the
// submission plan, and the draw area the frame would have to draw into. A second copy of that in
// the reconstruction would be a picture that differs from the logic frame's for reasons nobody
// chose.
//
// WHAT IT DOES NOT OWN. Guest-state publication. Committing the transformed-actor marks and the
// Moby shadow list stays with the caller that owns the surrounding transaction; reconstruction
// commits nothing.
#pragma once

#include "actor_stage.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_submitter.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::field_shaded_queue_emit {

inline constexpr uint32_t kProducerKey = 0x80022a2cu;
inline constexpr const char *kProducerName = "spriteq:world-shaded";

// Shared with every other producer's emission stage, so one refusal reads the same everywhere.
using Status = actor_stage::Emit;

struct Prepared {
  Status status = Status::ValidEmpty;
  field_shaded_queue_recipe::Recipe recipe;
  field_shaded_queue_submitter::Plan submitter;
};

// Derives the recipe and preflights its submission. `interval` is null on a logic frame, where
// every record is projected through its own transform; a reconstruction passes the interval its
// pairing produced.
Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 const field_shaded_queue_recipe::Input &input,
                 const field_shaded_queue_recipe::Interval *interval = nullptr);

// Publishes a Ready plan under the producer's scope. A ValidEmpty plan publishes nothing and is not
// a failure: a corpus whose every candidate was rejected still completed. Any other status is a
// programming error at the call site, which must have refused before reaching here.
void publish(Core &core, RenderQueue &queue, const Prepared &prepared);

} // namespace spyro::field_shaded_queue_emit
