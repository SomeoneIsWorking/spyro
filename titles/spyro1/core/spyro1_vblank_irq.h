#pragma once

#include <cstdint>

namespace spyro1 {

// A display field is represented by I_STAT/I_MASK bit 0. Other pending devices share the deferred
// IRQ gate but cannot authorize dispatch of the VBlank root.
constexpr bool hasPendingEnabledVblank(std::uint32_t iStat, std::uint32_t iMask) {
  constexpr std::uint32_t kVblankIrqMask = 1u;
  return (iStat & iMask & kVblankIrqMask) != 0;
}

} // namespace spyro1
