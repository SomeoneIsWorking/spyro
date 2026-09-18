#include "actor_temporal.h"

#include "fx_actor_draw.h"

#include <lucent/log.h>
#include <unordered_map>
#include <vector>

namespace spyro::actor_temporal {
namespace {} // namespace

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid-empty";
  case Status::NoEndpoints:
    return "no-endpoints";
  case Status::Recipe:
    return "recipe";
  case Status::Submission:
    return "submission";
  case Status::DrawArea:
    return "draw-area";
  }
  return "unknown";
}

const char *mismatchName(Mismatch mismatch) {
  switch (mismatch) {
  case Mismatch::None:
    return "none";
  case Mismatch::Descriptor:
    return "descriptor";
  case Mismatch::VertexCount:
    return "vertex-count";
  case Mismatch::CoordShift:
    return "coord-shift";
  case Mismatch::PrimitiveCount:
    return "primitive-count";
  }
  return "unknown";
}

Mismatch Census::worstMismatch() const {
  Mismatch worst = Mismatch::None;
  uint32_t best = 0;
  for (size_t i = 1; i < mismatches.size(); ++i) {
    if (mismatches[i] > best) {
      best = mismatches[i];
      worst = (Mismatch)i;
    }
  }
  return worst;
}

Mismatch mismatch(const actor_recipe_capture::Record &previous,
                  const actor_recipe_capture::Record &current) {
  const auto &a = previous.input;
  const auto &b = current.input;
  // The guest model descriptor: the same address is the same model, and the vertex count and both
  // stream shifts are read out of it, so they cannot disagree once this does not.
  if (previous.descriptor != current.descriptor) {
    return Mismatch::Descriptor;
  }
  if (a.vertexCount == 0 || a.vertexCount != b.vertexCount) {
    return Mismatch::VertexCount;
  }
  // Only the header's top byte is identity: the coordinate shift the depth key is expressed in,
  // and the clip-mode sign. The rest of the word is per-frame animation and transform state.
  if ((a.header >> 24) != (b.header >> 24)) {
    return Mismatch::CoordShift;
  }
  if (a.primitiveWords.size() != b.primitiveWords.size()) {
    return Mismatch::PrimitiveCount;
  }
  return Mismatch::None;
}

bool compatible(const actor_recipe_capture::Record &previous,
                const actor_recipe_capture::Record &current) {
  return mismatch(previous, current) == Mismatch::None;
}

void sample_records(const Endpoint &previous,
                    const Endpoint &current,
                    double t,
                    std::vector<actor_recipe_capture::Record> &sampled,
                    Census &census) {
  census = {};
  census.actors = (uint32_t)current.records.size();
  sampled = current.records;
  // A Moby drawn more than once in one frame is more than one record, so identity alone cannot
  // say which pose belongs to which draw. Occurrence order can: the producer walks its source in a
  // stable order, so the k-th draw of an instance this frame is the k-th draw of it last frame.
  std::unordered_map<uint32_t, std::vector<const actor_recipe_capture::Record *>> byMoby;
  byMoby.reserve(previous.records.size());
  for (const auto &record : previous.records) {
    if (record.moby != 0) {
      byMoby[record.moby].push_back(&record);
    }
  }
  std::unordered_map<uint32_t, size_t> consumed;
  for (auto &record : sampled) {
    if (record.moby == 0) {
      // A record with no instance identity cannot be matched to anything, this frame or the last.
      ++census.unpaired;
      continue;
    }
    const auto found = byMoby.find(record.moby);
    // The cursor advances on every draw of this instance, paired or not: occurrence order is the
    // pairing rule, and compatibility is a separate question about the pair it produced.
    const size_t occurrence = consumed[record.moby]++;
    if (found == byMoby.end() || occurrence >= found->second.size()) {
      ++census.unpaired;
      continue;
    }
    const auto &endpoint = *found->second[occurrence];
    const auto reason = mismatch(endpoint, record);
    if (reason != Mismatch::None) {
      ++census.incompatible;
      ++census.mismatches[(size_t)reason];
      continue;
    }
    const auto candidate = actor_prefix::sample(endpoint.input, record.input, t);
    if (candidate.status != actor_prefix::Status::Ok &&
        candidate.status != actor_prefix::Status::VisibilityRejected) {
      // The sampled pose left the projection's accepted range. The record keeps the endpoint its
      // own frame already built, which is a picture the queue is known to accept.
      ++census.refused;
      continue;
    }
    record.expected = candidate;
    ++census.interpolated;
  }
}

void History::begin(uint64_t scene, bool reference, bool active) {
  ++serial_;
  eligible = false;
  current_.reset();
  seen_ = false;
  refused_ = false;
  const bool enabled = active && !reference;
  if (!enabled || enabled != active_ || scene != scene_) {
    previous_.reset();
  }
  scene_ = scene;
  active_ = enabled;
}

void History::retain(std::vector<actor_recipe_capture::Record> records) {
  if (!active_) {
    return;
  }
  if (seen_ || refused_) {
    // Two regular-actor submissions in one logic frame compose one picture, and interpolating only
    // the first half of it would be worse than not interpolating at all.
    refuse();
    return;
  }
  seen_ = true;
  current_ = Endpoint{std::move(records), serial_};
}

void History::refuse() {
  current_.reset();
  eligible = false;
  refused_ = true;
}

void History::rotate() {
  previous_ = std::move(current_);
  current_.reset();
  eligible = false;
}

bool History::paired() const {
  return active_ && !refused_ && previous_ && current_ && previous_->serial + 1 == current_->serial;
}

Status History::emit(Core &core, RenderQueue &target, double t, Census &census) const {
  census = {};
  if (!paired()) {
    return Status::NoEndpoints;
  }
  std::vector<actor_recipe_capture::Record> sampled;
  sample_records(*previous_, *current_, t, sampled, census);
  const auto prepared = actor_emit::prepare(core, target, actor_draw::kProducerKey, sampled);
  switch (prepared.status) {
  case actor_emit::Status::ValidEmpty:
    return Status::ValidEmpty;
  case actor_emit::Status::Recipe:
    return Status::Recipe;
  case actor_emit::Status::Submission:
    return Status::Submission;
  case actor_emit::Status::DrawArea:
    return Status::DrawArea;
  case actor_emit::Status::Ready:
    break;
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
                mismatchName(worst),
                census.mismatches[(size_t)worst]);
  return Status::Ready;
}

} // namespace spyro::actor_temporal
