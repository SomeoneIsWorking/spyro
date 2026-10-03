// The route an actor face corpus takes to the render queue, for every producer that draws
// compressed-model actors.
//
// WHY IT IS ITS OWN OWNER. The regular layer (0x8001F798) and the secondary layer (0x80020F34)
// derive their faces from different guest programs, so their RECIPES are genuinely different code.
// What happens after a recipe is ready is not: preflight the submission plan against the queue,
// refuse a frame whose draw area is inverted, then publish under the producer's scope at the
// layer's own painter band. Both logic-frame producers and both temporal reconstructions take that
// route, which is four call sites for one policy.
//
// THE DRAW AREA IS PART OF THE PREFLIGHT, NOT OF PUBLICATION. An inverted draw area means the frame
// has no place to put these faces; discovering it after publication would leave the queue holding
// them.
#pragma once

#include "actor_draw_recipe.h"
#include "actor_face_submitter.h"
#include "actor_prefix_builder.h"
#include "actor_stage.h"

#include <cstdint>
#include <span>

class Core;
struct RenderQueue;

namespace spyro::actor_submission {

// Shared with every other owner on this route, so one refusal reads the same everywhere. This
// stage can only decline for its own two reasons; it never derives a recipe.
using Status = actor_stage::Emit;

struct Plan {
  Status status = Status::Submission;
  actor_face_submitter::Plan submitter;
};

Plan prepare(const Core &core,
             const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const actor_prefix::Output> outputs,
             std::span<const actor_draw_recipe::Face> faces);

// Publishes a Ready plan under the producer's scope. A ValidEmpty plan publishes nothing and is not
// a failure: a corpus whose every face was rejected still completed. Any other status is a
// programming error at the call site, which must have refused before reaching here.
void publish(Core &core,
             RenderQueue &queue,
             uint32_t producerKey,
             const char *producerName,
             actor_face_submitter::Layer layer,
             std::span<const actor_draw_recipe::Face> faces,
             const Plan &plan);

} // namespace spyro::actor_submission
