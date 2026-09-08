#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace spyro::actor_ot_coalescer {

constexpr uint32_t kLocalBinCount = 288u;
constexpr uint32_t kGlobalBinCount = 2048u;

struct Input {
  uint32_t localBaseOffset = 0; // bytes relative to the local pair table, not a guest pointer
  uint32_t depthNear = 0;
  uint32_t control = 0;
};

struct Result {
  bool valid = false;
  std::vector<uint16_t> bins; // one global bin per supplied local bin, in input order
};

// The title's local-pair splice scans fixed-size chunks, including unoccupied buckets.
// This models ordering only: it neither reads nor writes guest OT pairs or packet tags.
Result map(Input input, std::span<const uint32_t> localBins);

} // namespace spyro::actor_ot_coalescer
