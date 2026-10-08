#include "guest_execution.h"

#include "core.h"
#include "lightrec_executor.h"

#include <lucent/log.h>

namespace spyro {

GuestExecution::GuestExecution(Core &core, std::uint32_t entry)
    : core_(core), returnAddress_(core.r[31]), nextAddress_(entry) {}

psx::cpu::ExecutionResult GuestExecution::step(psx::cpu::ExecutionBudget budget) {
  if (stopped_) {
    return *stopped_;
  }
  auto result = core_.lightrecExecutor().executeFunction(nextAddress_, returnAddress_, budget);
  nextAddress_ = result.guestPc;
  if (result.reason != psx::cpu::ExecutionExitReason::BudgetExhausted) {
    stopped_ = result;
  }
  return result;
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
