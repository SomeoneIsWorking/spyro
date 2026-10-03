// Pairing and sampling for compressed-model actor records. See actor_pairing.h for the measured
// rule behind `Mismatch` and for why the pairing takes pointers.
#include "actor_pairing.h"

#include "actor_prefix_builder.h"

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
  const auto identity = [](const actor_recipe_capture::Record &record) {
    return record.moby;
  };
  std::array<uint32_t, kMismatchCount> reasons{};
  const auto admit = [&reasons](const actor_recipe_capture::Record &endpoint,
                                const actor_recipe_capture::Record &record) {
    const auto reason = mismatch(endpoint, record);
    if (reason != Mismatch::None) {
      ++reasons[(size_t)reason];
      return false;
    }
    return true;
  };
  const auto apply = [t](const actor_recipe_capture::Record &endpoint,
                         actor_recipe_capture::Record &record) {
    const auto candidate = actor_prefix::sample(endpoint.input, record.input, t);
    if (candidate.status != actor_prefix::Status::Ok &&
        candidate.status != actor_prefix::Status::VisibilityRejected) {
      // The sampled pose left the projection's accepted range. The record keeps the endpoint its
      // own frame already built, which is a picture the queue is known to accept.
      return false;
    }
    record.expected = candidate;
    return true;
  };
  instance_pairing::walk(previous, current, census, identity, admit, apply);
  census.mismatches = reasons;
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
