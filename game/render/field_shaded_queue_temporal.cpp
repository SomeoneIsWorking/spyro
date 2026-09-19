#include "field_shaded_queue_temporal.h"

#include "field_shaded_queue_emit.h"

#include <array>
#include <lucent/log.h>
#include <span>

namespace spyro::field_shaded_queue_temporal {

using Record = field_shaded_queue_recipe::Record;

const char *mismatchName(Mismatch mismatch) {
  switch (mismatch) {
  case Mismatch::None:
    return "none";
  case Mismatch::MeshIndex:
    return "mesh-index";
  case Mismatch::VertexCount:
    return "vertex-count";
  case Mismatch::PrimitiveCount:
    return "primitive-count";
  case Mismatch::ClipMode:
    return "clip-mode";
  }
  return "unknown";
}

Mismatch mismatch(const Record &previous, const Record &current) {
  if (previous.meshIndex != current.meshIndex) {
    return Mismatch::MeshIndex;
  }
  // The interval indexes both corpora by the same vertex ordinal, so a corpus that changed size is
  // a different mesh however its index reads.
  if (previous.vertices.empty() || previous.vertices.size() != current.vertices.size()) {
    return Mismatch::VertexCount;
  }
  if (previous.primitives.size() != current.primitives.size()) {
    return Mismatch::PrimitiveCount;
  }
  // Clip mode decides which of two projection programs the guest ran, so two records in different
  // modes were not drawn through the same transform pipeline at all.
  if (previous.clipMode != current.clipMode) {
    return Mismatch::ClipMode;
  }
  return Mismatch::None;
}

std::vector<const Record *>
pair(const Endpoint &previous, const Endpoint &current, Census &census) {
  // `walk` matches by occurrence over pointers, and both sides must carry the slot the answer goes
  // into, so one type crosses the boundary and the predecessor is written into the current slot.
  struct Slot {
    const Record *record = nullptr;
    const Record *endpoint = nullptr;
  };
  std::vector<Slot> before(previous.records.size());
  std::vector<const Slot *> beforeCursor;
  beforeCursor.reserve(before.size());
  for (size_t i = 0; i < previous.records.size(); ++i) {
    before[i].record = &previous.records[i];
    beforeCursor.push_back(&before[i]);
  }
  std::vector<Slot> after(current.records.size());
  std::vector<Slot *> afterCursor;
  afterCursor.reserve(after.size());
  for (size_t i = 0; i < current.records.size(); ++i) {
    after[i].record = &current.records[i];
    afterCursor.push_back(&after[i]);
  }

  std::array<uint32_t, kMismatchCount> reasons{};
  const auto identity = [](const Slot &slot) {
    return slot.record->actor;
  };
  const auto admit = [&reasons](const Slot &endpoint, const Slot &slot) {
    const auto reason = mismatch(*endpoint.record, *slot.record);
    if (reason != Mismatch::None) {
      ++reasons[(size_t)reason];
      return false;
    }
    return true;
  };
  const auto apply = [](const Slot &endpoint, Slot &slot) {
    slot.endpoint = endpoint.record;
    return true;
  };
  instance_pairing::walk(std::span<const Slot *const>(beforeCursor),
                         std::span<Slot *const>(afterCursor),
                         census,
                         identity,
                         admit,
                         apply);
  census.mismatches = reasons;

  std::vector<const Record *> predecessors;
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
  const std::vector<const Record *> predecessors = pair(*previous(), *current(), census);
  const field_shaded_queue_recipe::Interval interval{
      .previous = std::span<const Record *const>(predecessors), .t = t};
  const auto prepared = field_shaded_queue_emit::prepare(core, target, *current(), &interval);
  // A refusal is reported in the shared vocabulary, and a completed-empty corpus is reported
  // without publishing: there is nothing to put in the queue.
  if (prepared.status != field_shaded_queue_emit::Status::Ready) {
    return actor_stage::promote(prepared.status);
  }
  field_shaded_queue_emit::publish(core, target, prepared);
  const auto worst = census.worstMismatch();
  lucent::debug("shadedtemporal",
                "emit t={} records={} interpolated={} unpaired={} incompatible={} refused={} "
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

} // namespace spyro::field_shaded_queue_temporal
