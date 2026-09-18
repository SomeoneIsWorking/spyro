#include "secondary_actor_temporal.h"

#include "secondary_actor_emit.h"

#include <lucent/log.h>
#include <vector>

namespace spyro::secondary_actor_temporal {

void sample(const Endpoint &previous, Endpoint &current, double t, actor_pairing::Census &census) {
  // A secondary record keeps its actor record inside a larger per-actor struct, so the pairing is
  // handed pointers to the inner records rather than a flat vector. The rule itself is the shared
  // one; nothing about pairing is specific to this layer.
  std::vector<const actor_recipe_capture::Record *> before;
  before.reserve(previous.records.size());
  for (const auto &record : previous.records) {
    before.push_back(&record.actor);
  }
  std::vector<actor_recipe_capture::Record *> after;
  after.reserve(current.records.size());
  for (auto &record : current.records) {
    after.push_back(&record.actor);
  }
  actor_pairing::sample(std::span<const actor_recipe_capture::Record *const>(before),
                        std::span<actor_recipe_capture::Record *const>(after),
                        t,
                        census);
}

Status
History::emit(Core &core, RenderQueue &target, double t, actor_pairing::Census &census) const {
  census = {};
  if (!paired()) {
    return Status::NoEndpoints;
  }
  Endpoint sampled = *current();
  sample(*previous(), sampled, t, census);
  const auto prepared = secondary_actor_emit::prepare(core, target, sampled);
  // A refusal is reported in the shared vocabulary, and a completed-empty corpus is reported
  // without publishing: there is nothing to put in the queue.
  if (prepared.status != secondary_actor_emit::Status::Ready) {
    return actor_stage::promote(prepared.status);
  }
  secondary_actor_emit::publish(core, target, prepared);
  const auto worst = census.worstMismatch();
  lucent::debug("secondarytemporal",
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

} // namespace spyro::secondary_actor_temporal
