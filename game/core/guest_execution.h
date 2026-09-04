#pragma once

#include "execution_exit.h"

#include <cstdint>
#include <string_view>

class Core;

namespace spyro {

class GuestExecution {
public:
  explicit GuestExecution(Core &core);

  psx::cpu::ExecutionResult enter(std::uint32_t address);
  psx::cpu::ExecutionResult callOriginal(std::uint32_t address);

private:
  Core &core_;
};

bool reportExecutionResult(const psx::cpu::ExecutionResult &result, std::string_view owner);

} // namespace spyro
