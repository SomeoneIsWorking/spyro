#pragma once

#include <cstdint>

namespace spyro1 {

// A display field is represented by I_STAT/I_MASK bit 0. Other pending devices share the deferred
// IRQ gate but must not change whether the FieldScheduler dispatches the VBlank root directly.
constexpr bool hasPendingEnabledVblank(std::uint32_t iStat, std::uint32_t iMask) {
  constexpr std::uint32_t kVblankIrqMask = 1u;
  return (iStat & iMask & kVblankIrqMask) != 0;
}

// HookEntryInt's saved context is the only route that resumes the interrupted guest frame. Without
// one, an enabled VBlank does not replace the scheduler's host-owned direct root dispatch.
constexpr bool shouldDispatchVblankThroughIrq(std::uint32_t iStat,
                                              std::uint32_t iMask,
                                              std::uint32_t hookEntryIntBuffer) {
  return hookEntryIntBuffer != 0 && hasPendingEnabledVblank(iStat, iMask);
}

} // namespace spyro1
