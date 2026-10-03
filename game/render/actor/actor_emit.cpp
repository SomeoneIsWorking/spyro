#include "actor_emit.h"

#include "core.h"
#include "game.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::actor_emit {

Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 uint32_t producerKey,
                 std::span<const actor_recipe_capture::Record> records) {
  Prepared prepared{};
  prepared.recipe = actor_recipe_capture::compose_records(records, prepared.outputs);
  if (prepared.recipe.status == actor_draw_recipe::Status::ValidEmpty) {
    // There are no faces to plan, so the plan reports the same completed-empty state rather than
    // the never-ran default. Publication then has one state to read, not two.
    prepared.status = Status::ValidEmpty;
    prepared.plan.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.recipe.status != actor_draw_recipe::Status::Ready) {
    prepared.status = Status::Recipe;
    return prepared;
  }
  // The planner reports its refusal in the same vocabulary, so it is carried, not translated.
  prepared.plan =
      actor_submission::prepare(core, queue, producerKey, prepared.outputs, prepared.recipe.faces);
  prepared.status = prepared.plan.status;
  return prepared;
}

void publish(Core &core,
             RenderQueue &queue,
             uint32_t producerKey,
             const char *producerName,
             const Prepared &prepared) {
  if (prepared.status != Status::Ready && prepared.status != Status::ValidEmpty) {
    lucent::error("actordirect",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  producerKey,
                  actor_stage::name(prepared.status));
    std::abort();
  }
  actor_submission::publish(core,
                            queue,
                            producerKey,
                            producerName,
                            actor_face_submitter::Layer::Regular,
                            prepared.recipe.faces,
                            prepared.plan);
}

} // namespace spyro::actor_emit
