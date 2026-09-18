// Pairing and sampling for compressed-model actor records. See actor_pairing.h for the measured
// rule behind `Mismatch` and for why the pairing takes pointers.
#include "actor_pairing.h"

#include "actor_prefix_builder.h"

#include <unordered_map>

namespace spyro::actor_pairing {

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

void sample(std::span<const actor_recipe_capture::Record *const> previous,
            std::span<actor_recipe_capture::Record *const> current,
            double t,
            Census &census) {
  census = {};
  census.actors = (uint32_t)current.size();
  // A Moby drawn more than once in one frame is more than one record, so identity alone cannot
  // say which pose belongs to which draw. Occurrence order can: the producer walks its source in a
  // stable order, so the k-th draw of an instance this frame is the k-th draw of it last frame.
  std::unordered_map<uint32_t, std::vector<const actor_recipe_capture::Record *>> byMoby;
  byMoby.reserve(previous.size());
  for (const auto *record : previous) {
    if (record->moby != 0) {
      byMoby[record->moby].push_back(record);
    }
  }
  std::unordered_map<uint32_t, size_t> consumed;
  for (auto *record : current) {
    if (record->moby == 0) {
      // A record with no instance identity cannot be matched to anything, this frame or the last.
      ++census.unpaired;
      continue;
    }
    const auto found = byMoby.find(record->moby);
    // The cursor advances on every draw of this instance, paired or not: occurrence order is the
    // pairing rule, and compatibility is a separate question about the pair it produced.
    const size_t occurrence = consumed[record->moby]++;
    if (found == byMoby.end() || occurrence >= found->second.size()) {
      ++census.unpaired;
      continue;
    }
    const auto &endpoint = *found->second[occurrence];
    const auto reason = mismatch(endpoint, *record);
    if (reason != Mismatch::None) {
      ++census.incompatible;
      ++census.mismatches[(size_t)reason];
      continue;
    }
    const auto candidate = actor_prefix::sample(endpoint.input, record->input, t);
    if (candidate.status != actor_prefix::Status::Ok &&
        candidate.status != actor_prefix::Status::VisibilityRejected) {
      // The sampled pose left the projection's accepted range. The record keeps the endpoint its
      // own frame already built, which is a picture the queue is known to accept.
      ++census.refused;
      continue;
    }
    record->expected = candidate;
    ++census.interpolated;
  }
}

void sample(const std::vector<actor_recipe_capture::Record> &previous,
            std::vector<actor_recipe_capture::Record> &current,
            double t,
            Census &census) {
  std::vector<const actor_recipe_capture::Record *> before;
  before.reserve(previous.size());
  for (const auto &record : previous) {
    before.push_back(&record);
  }
  std::vector<actor_recipe_capture::Record *> after;
  after.reserve(current.size());
  for (auto &record : current) {
    after.push_back(&record);
  }
  sample(std::span<const actor_recipe_capture::Record *const>(before),
         std::span<actor_recipe_capture::Record *const>(after),
         t,
         census);
}

} // namespace spyro::actor_pairing
