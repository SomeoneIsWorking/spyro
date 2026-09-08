#include "fx_secondary_actor.h"

#include "actor_face_submitter.h"
#include "actor_prefix_builder.h"
#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "secondary_actor_recipe.h"
#include "secondary_actor_scene.h"

#include <algorithm>
#include <cstdint>
#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x80020f34u;

} // namespace

bool spyro_secondary_actor_submit(Core *core) {
  spyro::secondary_actor_scene::Frame frame{};
  const auto sceneStatus = spyro::secondary_actor_scene::prepare(core, frame);
  if (sceneStatus != spyro::secondary_actor_scene::Status::Ready) {
    lucent::debug("secondaryactor",
                  "REFUSED scene={} scanned={} queued={} culled={} coarse={} view={} invalid={}",
                  spyro::secondary_actor_scene::status_name(sceneStatus),
                  frame.census.scanned,
                  frame.census.queued,
                  frame.census.culled,
                  frame.census.coarseCulled,
                  frame.census.viewCulled,
                  frame.census.invalidModel);
    return false;
  }
  if (gpu_vk_wide_engine(core)) {
    const int32_t center = gpu_vk_wide_engine_w(core) / 2;
    for (auto &record : frame.records) {
      record.actor.input.projection.ofx = center << 16;
      record.actor.expected = spyro::actor_prefix::build(record.actor.input);
    }
  }
  const auto recipe = spyro::secondary_actor_recipe::derive(frame);
  if (recipe.status != spyro::secondary_actor_recipe::Status::Ready &&
      recipe.status != spyro::secondary_actor_recipe::Status::ValidEmpty) {
    lucent::debug("secondaryactor",
                  "REFUSED recipe={} reason={} record={} source_word={} records={} candidates={}",
                  (uint32_t)recipe.status,
                  (uint32_t)recipe.firstReason,
                  recipe.firstUnsupportedRecord,
                  recipe.firstUnsupportedSourceWord,
                  recipe.sourceRecords,
                  recipe.candidates);
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const auto plan =
      spyro::actor_face_submitter::prepare(queue, kProducerKey, recipe.outputs, recipe.faces);
  if (plan.status != spyro::actor_face_submitter::Status::Ready &&
      plan.status != spyro::actor_face_submitter::Status::ValidEmpty) {
    lucent::debug("secondaryactor", "REFUSED submission={}", (uint32_t)plan.status);
    return false;
  }
  const GpuState gpu = core->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }

  // Both guest-state publication and queue submission occur only after the
  // entire source, recipe, material, ordering, capacity, and draw-area call
  // have passed preflight.
  spyro::secondary_actor_scene::commit(core, frame);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "actor:secondary");
  spyro::actor_face_submitter::submit(
      core, queue, kProducerKey, spyro::actor_face_submitter::Layer::Secondary, recipe.faces, plan);
  lucent::debug("secondaryactor",
                "PASS visited={} records={} candidates={} rejected={} faces={} shadows={} "
                "painters_before={}",
                frame.visitedMobys.size(),
                frame.records.size(),
                recipe.candidates,
                recipe.rejectedCandidates,
                recipe.faces.size(),
                frame.shadows.size(),
                plan.admission.existingObjects);
  return true;
}
