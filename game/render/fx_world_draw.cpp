#include "fx_world_draw.h"

#include "core.h"
#include "game.h"
#include "producer_scope.h"
#include "spyro_context.h"
#include "world_scene_builder.h"
#include "world_scene_submitter.h"

#include <cstdint>
#include <lucent/log.h>
#include <utility>

namespace {

constexpr uint32_t kProducerKey = spyro::world_temporal::kProducerKey;

} // namespace

bool spyro_world_submit(Core *core, int32_t selection) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  auto &history = spyro_context(*core).worldTemporal;
  const auto animation = spyro::world_scene::animate(core, selection);
  if (!animation.ok) {
    lucent::debug("worlddirect", "REFUSED animation reason={}", animation.refusal);
    history.refuse();
    return false;
  }
  auto source = spyro::world_scene::capture(core, selection);
  const auto recipe = spyro::world_scene::build(source);
  const auto plan =
      spyro::world_scene_submitter::prepare(core, core->game->rq, kProducerKey, recipe);
  if (plan.status != spyro::world_scene_submitter::Status::Ready &&
      plan.status != spyro::world_scene_submitter::Status::ValidEmpty) {
    lucent::debug(
        "worlddirect",
        "REFUSED status={} reason={} selected={} low={} high={} candidates={} rejected={} "
        "submission={}",
        (uint32_t)recipe.status,
        recipe.refusal,
        recipe.selectedSectors,
        recipe.lowSectors,
        recipe.highSectors,
        recipe.candidates,
        recipe.rejected,
        (uint32_t)plan.status);
    history.refuse();
    return false;
  }
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "world:static");
  spyro::world_scene_submitter::submit(core, core->game->rq, kProducerKey, recipe, plan);
  history.retain(*core, std::move(source), plan.draw);
  lucent::debug("worlddirect",
                "PASS selected={} low={} high={} candidates={} rejected={} faces={} "
                "painters_before={}",
                recipe.selectedSectors,
                recipe.lowSectors,
                recipe.highSectors,
                recipe.candidates,
                recipe.rejected,
                recipe.faces.size(),
                plan.admission.existingObjects);
  return true;
}
