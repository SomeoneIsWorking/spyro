#include "secondary_actor_emit.h"
#include "wide_screen_space.h"

#include "actor_prefix_builder.h"
#include "core.h"
#include "face_light_environment.h"
#include "game.h"
#include "gpu_vk.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::secondary_actor_emit {

void recenter(Core &core, secondary_actor_scene::Frame &frame) {
  if (!gpu_vk_wide_engine(&core)) {
    return;
  }
  const int32_t center = spyro::wide_screen_space::horizontalCenter(&core);
  for (auto &record : frame.records) {
    record.actor.input.projection.ofx = center << 16;
    record.actor.expected = actor_prefix::build(record.actor.input);
  }
}

Prepared prepare(Core &core, const RenderQueue &queue, const secondary_actor_scene::Frame &frame) {
  Prepared prepared{};
  // One snapshot per call, which is the same data retail re-reads inside its face loop. The
  // reconstruction takes it at present time, when the globals still hold the logic frame whose
  // records it is sampling toward.
  const face_light::EnvironmentSource lighting(&core);
  prepared.recipe = secondary_actor_recipe::derive(frame, lighting.environment());
  if (prepared.recipe.status == secondary_actor_recipe::Status::ValidEmpty) {
    // There are no faces to plan, so the plan reports the same completed-empty state rather than
    // the never-ran default. Publication then has one state to read, not two.
    prepared.status = Status::ValidEmpty;
    prepared.plan.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.recipe.status != secondary_actor_recipe::Status::Ready) {
    prepared.status = Status::Recipe;
    return prepared;
  }
  // The planner reports its refusal in the same vocabulary, so it is carried, not translated.
  prepared.plan = actor_submission::prepare(
      core, queue, kProducerKey, prepared.recipe.outputs, prepared.recipe.faces);
  prepared.status = prepared.plan.status;
  return prepared;
}

void publish(Core &core, RenderQueue &queue, const Prepared &prepared) {
  if (prepared.status != Status::Ready && prepared.status != Status::ValidEmpty) {
    lucent::error("secondaryactor",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  kProducerKey,
                  actor_stage::name(prepared.status));
    std::abort();
  }
  actor_submission::publish(core,
                            queue,
                            kProducerKey,
                            kProducerName,
                            actor_face_submitter::Layer::Secondary,
                            prepared.recipe.faces,
                            prepared.plan);
}

} // namespace spyro::secondary_actor_emit
