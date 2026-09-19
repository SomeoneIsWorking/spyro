#include "terrain_temporal.h"

#include "terrain_emit.h"

#include <array>
#include <lucent/log.h>
#include <span>

namespace spyro::terrain_temporal {

using Object = terrain_recipe::Object;

const char *mismatchName(Mismatch mismatch) {
  switch (mismatch) {
  case Mismatch::None:
    return "none";
  case Mismatch::VertexCount:
    return "vertex-count";
  case Mismatch::FaceCount:
    return "face-count";
  }
  return "unknown";
}

Mismatch mismatch(const Object &previous, const Object &current) {
  // The interval indexes both corpora by the same vertex ordinal, so a corpus that changed size is
  // different geometry however its address reads.
  if (previous.vertices.empty() || previous.vertices.size() != current.vertices.size()) {
    return Mismatch::VertexCount;
  }
  if (previous.faces.size() != current.faces.size()) {
    return Mismatch::FaceCount;
  }
  return Mismatch::None;
}

std::vector<const Object *>
pair(const Endpoint &previous, const Endpoint &current, Census &census) {
  // `walk` matches by occurrence over pointers, and both sides must carry the slot the answer goes
  // into, so one type crosses the boundary and the predecessor is written into the current slot.
  struct Slot {
    const Object *object = nullptr;
    const Object *endpoint = nullptr;
  };
  std::vector<Slot> before(previous.objects.size());
  std::vector<const Slot *> beforeCursor;
  beforeCursor.reserve(before.size());
  for (size_t i = 0; i < previous.objects.size(); ++i) {
    before[i].object = &previous.objects[i];
    beforeCursor.push_back(&before[i]);
  }
  std::vector<Slot> after(current.objects.size());
  std::vector<Slot *> afterCursor;
  afterCursor.reserve(after.size());
  for (size_t i = 0; i < current.objects.size(); ++i) {
    after[i].object = &current.objects[i];
    afterCursor.push_back(&after[i]);
  }

  std::array<uint32_t, kMismatchCount> reasons{};
  const auto identity = [](const Slot &slot) {
    return slot.object->address;
  };
  const auto admit = [&reasons](const Slot &endpoint, const Slot &slot) {
    const auto reason = mismatch(*endpoint.object, *slot.object);
    if (reason != Mismatch::None) {
      ++reasons[(size_t)reason];
      return false;
    }
    return true;
  };
  const auto apply = [](const Slot &endpoint, Slot &slot) {
    slot.endpoint = endpoint.object;
    return true;
  };
  instance_pairing::walk(std::span<const Slot *const>(beforeCursor),
                         std::span<Slot *const>(afterCursor),
                         census,
                         identity,
                         admit,
                         apply);
  census.mismatches = reasons;

  std::vector<const Object *> predecessors;
  predecessors.reserve(after.size());
  for (const auto &slot : after) {
    predecessors.push_back(slot.endpoint);
  }
  return predecessors;
}

Status History::emit(Core &core, RenderQueue &target, double t, Census &census) const {
  census = {};
  if (!paired()) {
    return Status::NoEndpoints;
  }
  const std::vector<const Object *> predecessors = pair(*previous(), *current(), census);
  const terrain_recipe::Interval interval{.previous = std::span<const Object *const>(predecessors),
                                          .previousView = previous()->view,
                                          .t = t};
  const auto prepared = terrain_emit::prepare(core, target, *current(), &interval);
  // A refusal is reported in the shared vocabulary, and a completed-empty corpus is reported
  // without publishing: there is nothing to put in the queue.
  if (prepared.status != terrain_emit::Status::Ready) {
    return actor_stage::promote(prepared.status);
  }
  terrain_emit::publish(core, target, prepared);
  const auto worst = census.worstMismatch();
  lucent::debug("terraintemporal",
                "emit t={} objects={} interpolated={} unpaired={} incompatible={} refused={} "
                "sampled={} declined={} faces={} worst_mismatch={}x{}",
                t,
                census.actors,
                census.interpolated,
                census.unpaired,
                census.incompatible,
                census.refused,
                prepared.recipe.sampled,
                prepared.recipe.sampleDeclined,
                prepared.recipe.faces.size(),
                mismatchName(worst),
                census.mismatches[(size_t)worst]);
  return Status::Ready;
}

} // namespace spyro::terrain_temporal
