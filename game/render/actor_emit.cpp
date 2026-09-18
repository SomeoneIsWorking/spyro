#include "actor_emit.h"

#include "core.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_emit {

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid-empty";
  case Status::Recipe:
    return "recipe";
  case Status::Submission:
    return "submission";
  case Status::DrawArea:
    return "draw-area";
  }
  return "unknown";
}

Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 uint32_t producerKey,
                 std::span<const actor_recipe_capture::Record> records) {
  Prepared prepared{};
  prepared.recipe = actor_recipe_capture::compose_records(records, prepared.outputs);
  if (prepared.recipe.status == actor_draw_recipe::Status::ValidEmpty) {
    prepared.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.recipe.status != actor_draw_recipe::Status::Ready) {
    prepared.status = Status::Recipe;
    return prepared;
  }
  prepared.plan =
      actor_face_submitter::prepare(queue, producerKey, prepared.outputs, prepared.recipe.faces);
  if (prepared.plan.status != actor_face_submitter::Status::Ready) {
    prepared.status = Status::Submission;
    return prepared;
  }
  // The destination is part of the plan: an inverted draw area means the frame has no place to put
  // these faces, and discovering that after publication would leave the queue holding them.
  const GpuState &gpu = core.game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
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
             const Prepared &prepared) {
  if (prepared.status == Status::ValidEmpty) {
    return;
  }
  if (prepared.status != Status::Ready) {
    lucent::error("actordirect",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  producerKey,
                  statusName(prepared.status));
    std::abort();
  }
  ProducerScope producer(&core.rsub.producerScope, producerKey, producerName);
  actor_face_submitter::submit(&core,
                               queue,
                               producerKey,
                               actor_face_submitter::Layer::Regular,
                               prepared.recipe.faces,
                               prepared.plan);
}

} // namespace spyro::actor_emit
