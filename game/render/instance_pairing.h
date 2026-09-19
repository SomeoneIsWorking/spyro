// Pairing one frame's draw records to the frame before them, by guest instance in occurrence order.
//
// WHAT IS SHARED AND WHAT IS NOT. Every temporal source in this title has to answer the same
// question before it can sample anything: which of last frame's records IS this record. The answer
// does not depend on what a record contains. A producer walks its source in a stable order, so the
// k-th draw of instance X this frame is the k-th draw of X last frame — and a Moby drawn twice is
// two records that identity alone cannot tell apart. What DOES depend on the layer is the identity
// rule (which fields must match before two poses may be blended) and the sampler itself, so both
// arrive as callables.
//
// THE CURSOR ADVANCES ON EVERY DRAW, PAIRED OR NOT. Occurrence order is the pairing rule;
// compatibility is a separate question about the pair it produced. Skipping the cursor on an
// incompatible pair would silently re-pair the next draw of that instance against the record that
// just failed.
//
// PURE. No Core, no queue, no guest memory: a record is already a deep semantic copy.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace spyro::instance_pairing {

// How one frame's records were accounted for. Every record falls in exactly one bucket, so
// `interpolated + unpaired + incompatible + refused == actors` is an invariant a caller may assert.
// A bare "not interpolated" count cannot be told from a rule that never ran, which is why the
// refusals are split.
struct Census {
  uint32_t actors = 0;       // records in the current frame
  uint32_t interpolated = 0; // paired with a compatible predecessor, and the sampler accepted
  uint32_t unpaired = 0;     // the frame before it drew this instance fewer times, or not at all
  uint32_t incompatible = 0; // a predecessor existed but the layer's identity rule rejected it
  uint32_t refused = 0;      // the sampler declined, so the record keeps its own endpoint
};

// The shared accounting plus the one thing only a layer can say: WHICH field rejected a pair.
// `incompatible` is split by reason, indexed by the layer's own enum, whose slot 0 is its
// compatible case and stays zero. Every layer needs this and none of them needs a different
// implementation of it, so the counting and the "which reason dominated" question live here and
// each layer supplies only the enum and the names.
template <class Reason, size_t Count> struct ReasonedCensus : Census {
  std::array<uint32_t, Count> mismatches{};

  // The reason that rejected the most records, or slot 0's value when none were rejected.
  Reason worstMismatch() const {
    Reason worst = (Reason)0;
    uint32_t best = 0;
    for (size_t i = 1; i < Count; ++i) {
      if (mismatches[i] > best) {
        best = mismatches[i];
        worst = (Reason)i;
      }
    }
    return worst;
  }
};

// `identity(record) -> uint32_t`, where 0 means the producer could not attribute this draw to an
// instance and it is unpairable on both sides.
// `admit(previous, current) -> bool`: the layer's identity rule. It records its own reason.
// `sample(previous, current) -> bool`: false when the layer's sampler declined the pair.
template <class Previous, class Current, class Identity, class Admit, class Sample>
void walk(std::span<Previous *const> previous,
          std::span<Current *const> current,
          Census &census,
          Identity identity,
          Admit admit,
          Sample sample) {
  census = {};
  census.actors = (uint32_t)current.size();
  std::unordered_map<uint32_t, std::vector<Previous *>> byInstance;
  byInstance.reserve(previous.size());
  for (auto *record : previous) {
    const uint32_t instance = identity(*record);
    if (instance != 0) {
      byInstance[instance].push_back(record);
    }
  }
  std::unordered_map<uint32_t, size_t> consumed;
  for (auto *record : current) {
    const uint32_t instance = identity(*record);
    if (instance == 0) {
      ++census.unpaired;
      continue;
    }
    const auto found = byInstance.find(instance);
    const size_t occurrence = consumed[instance]++;
    if (found == byInstance.end() || occurrence >= found->second.size()) {
      ++census.unpaired;
      continue;
    }
    const auto &endpoint = *found->second[occurrence];
    if (!admit(endpoint, *record)) {
      ++census.incompatible;
      continue;
    }
    if (!sample(endpoint, *record)) {
      ++census.refused;
      continue;
    }
    ++census.interpolated;
  }
}

} // namespace spyro::instance_pairing
