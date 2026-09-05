#pragma once

#include "execution_exit.h"

#include <cstdint>
#include <optional>
#include <string_view>

class Core;

namespace spyro {

class GuestExecution {
public:
  GuestExecution(Core &core, std::uint32_t entry);

  // Budget yields preserve the root return boundary even inside a nested guest call.
  // Other exits are handed back to the caller and never resumed implicitly.
  psx::cpu::ExecutionResult step(psx::cpu::ExecutionBudget budget);

private:
  Core &core_;
  const std::uint32_t returnAddress_;
  std::uint32_t nextAddress_;
  std::optional<psx::cpu::ExecutionResult> stopped_;
};

bool reportExecutionResult(const psx::cpu::ExecutionResult &result, std::string_view owner);

} // namespace spyro
