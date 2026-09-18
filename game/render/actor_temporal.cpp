#include "actor_temporal.h"

#include "fx_actor_draw.h"

#include <lucent/log.h>

namespace spyro::actor_temporal {

Status
History::emit(Core &core, RenderQueue &target, double t, actor_pairing::Census &census) const {
  census = {};
  if (!paired()) {
    return Status::NoEndpoints;
  }
  Endpoint sampled = *current();
  actor_pairing::sample(*previous(), sampled, t, census);
  const auto prepared = actor_emit::prepare(core, target, actor_draw::kProducerKey, sampled);
  // A refusal is reported in the shared vocabulary, and a completed-empty corpus is reported
  // without publishing: there is nothing to put in the queue.
  if (prepared.status != actor_emit::Status::Ready) {
    return actor_stage::promote(prepared.status);
  }
  actor_emit::publish(core, target, actor_draw::kProducerKey, actor_draw::kProducerName, prepared);
  const auto worst = census.worstMismatch();
  lucent::debug("actortemporal",
                "emit t={} actors={} interpolated={} unpaired={} incompatible={} refused={} "
                "faces={} worst_mismatch={}x{}",
                t,
                census.actors,
                census.interpolated,
                census.unpaired,
                census.incompatible,
                census.refused,
                prepared.recipe.faces.size(),
                actor_pairing::mismatchName(worst),
                census.mismatches[(size_t)worst]);
  return Status::Ready;
}

} // namespace spyro::actor_temporal
