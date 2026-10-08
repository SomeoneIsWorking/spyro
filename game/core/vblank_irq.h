#pragma once

#include <cstdint>

namespace spyro {

// Other pending devices share the deferred IRQ gate but cannot authorize dispatch of a title's
// vblank root, so this states the one fact the shared field owner needs: is a VBlank edge pending
// AND enabled right now.
constexpr bool hasPendingEnabledVblank(std::uint32_t iStat, std::uint32_t iMask) {
  constexpr std::uint32_t kVblankIrqMask = 1u;
  return (iStat & iMask & kVblankIrqMask) != 0;
}

} // namespace spyro
