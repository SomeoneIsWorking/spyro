#include "actor_ot_coalescer.h"

#include <algorithm>
#include <array>

namespace spyro::actor_ot_coalescer {

Result map(Input input, std::span<const uint32_t> localBins) {
  Result result{};
  if (localBins.empty()) {
    result.valid = true;
    return result;
  }
  const auto [minimum, maximum] = std::minmax_element(localBins.begin(), localBins.end());
  if (*maximum >= kLocalBinCount || (input.localBaseOffset & 7u) != 0u) {
    return result;
  }
  const uint32_t shift = input.control & 31u;
  const uint32_t step = 256u >> shift;
  if (step < 8u) {
    return result;
  }
  const uint32_t minPair = input.localBaseOffset + *minimum * 8u;
  const uint32_t maxPair = input.localBaseOffset + *maximum * 8u;
  const uint32_t gap = 2048u - maxPair;
  const uint32_t adjust = static_cast<int32_t>(gap) < 0 ? 0u : ((gap << shift) >> 8u) << 3u;
  uint32_t offset = (input.depthNear << 3u) + ((32u << shift) - 8u) - adjust;
  if (static_cast<int32_t>(offset) < 0) {
    offset = 0;
  }
  if (offset / 8u >= kGlobalBinCount) {
    return result;
  }

  std::array<uint16_t, kLocalBinCount> mapped{};
  uint32_t scan = maxPair;
  const uint32_t terminal = minPair - 8u;
  while (true) {
    const uint32_t candidate = scan - step;
    const uint32_t limit = static_cast<int32_t>(terminal - candidate) > 0 ? terminal : candidate;
    for (uint32_t pair = scan; pair != limit; pair -= 8u) {
      const uint32_t bin = (pair - input.localBaseOffset) / 8u;
      if (bin >= kLocalBinCount) {
        return result;
      }
      mapped[bin] = static_cast<uint16_t>(offset / 8u);
    }
    if (limit == terminal) {
      break;
    }
    scan = limit;
    // 0x80025894 subtracts eight in the branch delay slot; the zero case adds it back.
    offset = offset == 0u ? 0u : offset - 8u;
  }
  result.bins.reserve(localBins.size());
  for (uint32_t bin : localBins) {
    result.bins.push_back(mapped[bin]);
  }
  result.valid = true;
  return result;
}

} // namespace spyro::actor_ot_coalescer
