#include "fx_field_shaded_queue.h"

#include "core.h"
#include "field_shaded_queue_recipe.h"
#include "field_shaded_queue_scene.h"
#include "field_shaded_queue_submitter.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"

#include <algorithm>
#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x80022a2cu;

} // namespace

bool spyro_field_shaded_queue_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  int32_t clipRight = 512;
  if (gpu_vk_wide_engine(core)) {
    clipRight = std::max(clipRight, gpu_vk_wide_engine_w(core));
  }
  spyro::field_shaded_queue_scene::Frame frame{};
  const auto sceneStatus = spyro::field_shaded_queue_scene::prepare(core, clipRight, frame);
  if (sceneStatus != spyro::field_shaded_queue_scene::Status::Ready) {
    lucent::debug("fieldshaded",
                  "REFUSED scene={}",
                  spyro::field_shaded_queue_scene::statusName(sceneStatus));
    return false;
  }
  const auto recipe = spyro::field_shaded_queue_recipe::derive(frame.input);
  if (recipe.status != spyro::field_shaded_queue_recipe::Status::Ready &&
      recipe.status != spyro::field_shaded_queue_recipe::Status::ValidEmpty) {
    lucent::debug("fieldshaded",
                  "REFUSED recipe={} actor=0x{:08X} primitive={} candidates={}",
                  (uint32_t)recipe.status,
                  recipe.firstUnsupportedActor,
                  recipe.firstUnsupportedPrimitive,
                  recipe.candidates);
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const auto plan = spyro::field_shaded_queue_submitter::prepare(queue, kProducerKey, recipe);
  if (plan.status != spyro::field_shaded_queue_submitter::Status::Ready &&
      plan.status != spyro::field_shaded_queue_submitter::Status::ValidEmpty) {
    return false;
  }
  const GpuState gpu = core->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }

  spyro::field_shaded_queue_scene::commit(core, frame);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "spriteq:world-shaded");
  spyro::field_shaded_queue_submitter::submit(core, queue, kProducerKey, recipe, plan);
  lucent::debug("fieldshaded",
                "PASS queue={} screen={} world={} null={} culled={} candidates={} rejected={} "
                "faces={} shadows={}",
                frame.queueRecords,
                frame.screenRecords,
                frame.input.records.size(),
                frame.nullMeshes,
                frame.culled,
                recipe.candidates,
                recipe.rejected,
                recipe.faces.size(),
                frame.shadows.size());
  return true;
}
