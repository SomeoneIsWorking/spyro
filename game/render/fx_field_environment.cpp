#include "fx_field_environment.h"

#include "core.h"
#include "field_environment_scene.h"
#include "field_scene_recipe.h"
#include "game.h"
#include "producer_scope.h"
#include "world_scene_submitter.h"

#include <cstdint>
#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x800258f0u;

} // namespace

bool spyro_field_environment_submit(Core *core) {
  if (core == nullptr || core->game == nullptr) {
    return false;
  }
  spyro::field_environment_scene::Frame frame{};
  const auto scene = spyro::field_environment_scene::prepare(core, frame);
  if (scene != spyro::field_environment_scene::Status::Ready &&
      scene != spyro::field_environment_scene::Status::ValidEmpty) {
    lucent::debug("fieldenv",
                  "REFUSED scene={} world={} reason={}",
                  spyro::field_environment_scene::statusName(scene),
                  (uint32_t)frame.world.status,
                  frame.world.refusal);
    lucent::debug("fieldenv",
                  "  animation ok={} reason={} channels={} direct={} blended={} writes={}",
                  frame.animation.ok,
                  frame.animation.refusal,
                  frame.animation.channels,
                  frame.animation.direct,
                  frame.animation.blended,
                  frame.animation.writes);
    return false;
  }
  const auto plan =
      spyro::world_scene_submitter::prepare(core, core->game->rq, kProducerKey, frame.world);
  if (plan.status != spyro::world_scene_submitter::Status::Ready &&
      plan.status != spyro::world_scene_submitter::Status::ValidEmpty) {
    return false;
  }

  spyro::field_scene_recipe::applyEnvironment(core, frame.invocation);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "field:environment");
  spyro::world_scene_submitter::submit(core, core->game->rq, kProducerKey, frame.world, plan);
  lucent::debug("fieldenv",
                "PASS selection={} distance=0x{:X} sectors={} low={} high={} candidates={} "
                "rejected={} faces={}",
                frame.invocation.worldSelection,
                frame.invocation.cullingDistance,
                frame.world.selectedSectors,
                frame.world.lowSectors,
                frame.world.highSectors,
                frame.world.candidates,
                frame.world.rejected,
                frame.world.faces.size());
  lucent::debug("fieldenv",
                "PASS animation channels={} direct={} blended={} writes={}",
                frame.animation.channels,
                frame.animation.direct,
                frame.animation.blended,
                frame.animation.writes);
  return true;
}
