#pragma once

#include "core.h"
#include "execution_control.h"
#include "native_dispatch.h"

#include <cstdlib>
#include <lucent/log.h>
#include <string_view>

namespace spyro {

inline bool dispatchGuestOrPropagate(Core &core, std::uint32_t address) {
  return psx::cpu::completeOrPropagate(
      core, psx::cpu::dispatchGuest(core, address, psx::cpu::ExecutionBudget::currentTurn(core)));
}

inline bool callOriginalOrPropagate(Core &core, std::uint32_t address) {
  return psx::cpu::completeOrPropagate(
      core, psx::cpu::callOriginal(core, address, psx::cpu::ExecutionBudget::currentTurn(core)));
}

inline void installNativeOverride(Core &core,
                                  std::uint32_t address,
                                  std::string_view name,
                                  psx::cpu::NativeFunction function) {
  const auto image = core.currentImageIdentity(address);
  if (!image) {
    lucent::error("override",
                  "cannot install {} at 0x{:08X}: no active image owns that address",
                  name,
                  address);
    std::abort();
  }
  if (!core.nativeDispatcher().install({psx::cpu::NativeKey{*image, address}, name, function})) {
    lucent::error("override",
                  "cannot install {} at 0x{:08X}: that image/address already has an owner",
                  name,
                  address);
    std::abort();
  }
}

} // namespace spyro
