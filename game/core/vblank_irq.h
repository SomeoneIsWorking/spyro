#pragma once

#include <cstdint>

namespace spyro {

// A display field is represented by I_STAT/I_MASK bit 0. Other pending devices share the deferred
// IRQ gate but cannot authorize dispatch of a title's vblank root, so this states the one fact
// the shared field owner needs: is a VBlank edge pending AND enabled right now.
//
// It was a Spyro 1 header until the field owner became shared, which is the same reason it lives
// in game/core: no title in this lineage can own a PSX register fact.
constexpr bool hasPendingEnabledVblank(std::uint32_t iStat, std::uint32_t iMask) {
  constexpr std::uint32_t kVblankIrqMask = 1u;
  return (iStat & iMask & kVblankIrqMask) != 0;
}

} // namespace spyro
