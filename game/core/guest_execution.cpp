#include "guest_execution.h"

#include "core.h"
#include "native_dispatch.h"

#include <lucent/log.h>

namespace spyro {

GuestExecution::GuestExecution(Core &core) : core_(core) {}

psx::cpu::ExecutionResult GuestExecution::enter(std::uint32_t address) {
  return psx::cpu::dispatchGuest(core_, address, psx::cpu::ExecutionBudget::currentTurn(core_));
}

psx::cpu::ExecutionResult GuestExecution::callOriginal(std::uint32_t address) {
  return psx::cpu::callOriginal(core_, address, psx::cpu::ExecutionBudget::currentTurn(core_));
}

bool reportExecutionResult(const psx::cpu::ExecutionResult &result, std::string_view owner) {
  if (result.returned() || result.reason == psx::cpu::ExecutionExitReason::ProcessExit) {
    return true;
  }
  lucent::error("executor",
                "{} stopped at guest PC 0x{:08X}: {} ({})",
                owner,
                result.guestPc,
                psx::cpu::executionExitName(result.reason),
                result.detail);
  return false;
}

} // namespace spyro
