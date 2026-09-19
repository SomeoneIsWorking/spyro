#include "terrain_emit.h"

#include "core.h"
#include "draw_area.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::terrain_emit {

Prepared prepare(const Core &core,
                 const RenderQueue &queue,
                 const terrain_recipe::Input &input,
                 const terrain_recipe::Interval *interval) {
  Prepared prepared{};
  prepared.projection = input.projection;
  prepared.recipe = terrain_recipe::derive(input, interval);
  if (prepared.recipe.status != terrain_recipe::Status::Ready &&
      prepared.recipe.status != terrain_recipe::Status::ValidEmpty) {
    prepared.status = Status::Recipe;
    return prepared;
  }
  prepared.submitter = terrain_submitter::prepare(queue, kProducerKey, prepared.recipe);
  if (prepared.submitter.status == terrain_submitter::Status::ValidEmpty) {
    // A completed picture of nothing, not a refusal: the planner reports ValidEmpty for exactly one
    // input, a corpus with no faces.
    prepared.status = Status::ValidEmpty;
    return prepared;
  }
  if (prepared.submitter.status != terrain_submitter::Status::Ready) {
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
    lucent::error("terraindirect",
                  "FATAL: publish of producer 0x{:08X} in state {}",
                  kProducerKey,
                  actor_stage::name(prepared.status));
    std::abort();
  }
  ProducerScope producer(&core.rsub.producerScope, kProducerKey, kProducerName);
  terrain_submitter::submit(
      &core, queue, kProducerKey, prepared.recipe, prepared.submitter, prepared.projection);
}

} // namespace spyro::terrain_emit
