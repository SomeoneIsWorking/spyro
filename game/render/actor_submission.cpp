#include "actor_submission.h"

#include "core.h"
#include "draw_area.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_submission {

Plan prepare(const Core &core,
             const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const actor_prefix::Output> outputs,
             std::span<const actor_draw_recipe::Face> faces) {
  Plan prepared{};
  prepared.submitter = actor_face_submitter::prepare(queue, producerKey, outputs, faces);
  if (prepared.submitter.status == actor_face_submitter::Status::ValidEmpty) {
    // The planner reports ValidEmpty for exactly one input: a corpus with no faces. A recipe can
    // reach that after rejecting every candidate, and it is a completed picture of nothing, not a
    // refusal — which is the answer the secondary layer already gave and the regular layer's own
    // shadow-list comment already assumed.
    prepared.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.submitter.status != actor_face_submitter::Status::Ready) {
    prepared.status = Status::Submission;
    return prepared;
  }
  const GpuState &gpu = core.game->gpu;
  if (!spyro::draw_area::ready(gpu)) {
    prepared.status = Status::DrawArea;
    return prepared;
  }
  prepared.status = Status::Ready;
  return prepared;
}

void publish(Core &core,
             RenderQueue &queue,
             uint32_t producerKey,
             const char *producerName,
             actor_face_submitter::Layer layer,
             std::span<const actor_draw_recipe::Face> faces,
             const Plan &plan) {
  if (plan.status == Status::ValidEmpty) {
    return;
  }
  if (plan.status != Status::Ready) {
    lucent::error("actorsubmission",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  producerKey,
                  actor_stage::name(plan.status));
    std::abort();
  }
  ProducerScope producer(&core.rsub.producerScope, producerKey, producerName);
  actor_face_submitter::submit(&core, queue, producerKey, layer, faces, plan.submitter);
}

} // namespace spyro::actor_submission
