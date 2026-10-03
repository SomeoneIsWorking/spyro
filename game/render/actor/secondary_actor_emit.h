// The secondary-actor layer's one compose/preflight/publish owner (0x800208FC + 0x80020F34).
//
// WHY IT EXISTS. Two callers reach this layer: the field composition, which draws it beside the
// world-shaded sprite queue and owns the guest-side shadow-list transaction the two share, and the
// temporal source, which reconstructs an in-between present and owns no guest state at all. What
// they must agree on is everything between a scene frame and the queue — the widescreen
// re-centring, the lighting snapshot the per-face colour program reads, the recipe, and the
// submission plan. A second copy of that in the reconstruction would be a picture that differs
// from the logic frame's for reasons nobody chose.
//
// WHAT IT DOES NOT OWN. Guest-state publication. The composition commits the actor transform-state
// bytes and the Moby shadow list itself, between its last refusal and the first queue mutation,
// because that transaction spans both of its layers. Reconstruction never commits anything.
#pragma once

#include "actor_submission.h"
#include "secondary_actor_recipe.h"
#include "secondary_actor_scene.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::secondary_actor_emit {

inline constexpr uint32_t kProducerKey = 0x80020f34u;
inline constexpr const char *kProducerName = "actor:secondary";

using Status = actor_stage::Emit;

struct Prepared {
  Status status = Status::ValidEmpty;
  secondary_actor_recipe::Recipe recipe;
  actor_submission::Plan plan;
};

// Widescreen re-centres every record's projection before anything is derived from it, so a
// reconstructed pose and a logic-frame pose are built through the same projection. It writes no
// guest memory: the record is already a deep copy.
void recenter(Core &core, secondary_actor_scene::Frame &frame);

// Snapshots the lighting globals the per-face colour program reads, derives the recipe, and
// preflights the submission. `core` is non-const because taking that snapshot reads guest memory.
Prepared prepare(Core &core, const RenderQueue &queue, const secondary_actor_scene::Frame &frame);

void publish(Core &core, RenderQueue &queue, const Prepared &prepared);

} // namespace spyro::secondary_actor_emit
