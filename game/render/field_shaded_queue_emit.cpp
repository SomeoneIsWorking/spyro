#include "field_shaded_queue_emit.h"

#include "core.h"
#include "draw_area.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::field_shaded_queue_emit {

Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 const field_shaded_queue_recipe::Input &input,
                 const field_shaded_queue_recipe::Interval *interval,
                 Pass pass) {
  Prepared prepared{};
  prepared.pass = pass;
  prepared.recipe = field_shaded_queue_recipe::derive(input, interval);
  if (prepared.recipe.status != field_shaded_queue_recipe::Status::Ready &&
      prepared.recipe.status != field_shaded_queue_recipe::Status::ValidEmpty) {
    prepared.status = Status::Recipe;
    return prepared;
  }
  prepared.submitter = field_shaded_queue_submitter::prepare(queue, pass.key, prepared.recipe);
  if (prepared.submitter.status == field_shaded_queue_submitter::Status::ValidEmpty) {
    // A completed picture of nothing, not a refusal: the planner reports ValidEmpty for exactly one
    // input, a corpus with no faces.
    prepared.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.submitter.status != field_shaded_queue_submitter::Status::Ready) {
    prepared.status = Status::Submission;
    return prepared;
  }
  if (!draw_area::ready(core.game->gpu)) {
    prepared.status = Status::DrawArea;
    return prepared;
  }
  prepared.status = Status::Ready;
  return prepared;
}

void publish(Core &core, RenderQueue &queue, const Prepared &prepared) {
  if (prepared.status == Status::ValidEmpty) {
    return;
  }
  if (prepared.status != Status::Ready) {
    lucent::error("fieldshaded",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  prepared.pass.key,
                  actor_stage::name(prepared.status));
    std::abort();
  }
  ProducerScope producer(&core.rsub.producerScope, prepared.pass.key, prepared.pass.name);
  field_shaded_queue_submitter::submit(
      &core, queue, prepared.pass.key, prepared.recipe, prepared.submitter);
}

} // namespace spyro::field_shaded_queue_emit
