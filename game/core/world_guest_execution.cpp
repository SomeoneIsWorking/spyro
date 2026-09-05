#include "world_guest_execution.h"

#include "native_dispatch.h"

namespace spyro {
namespace {
constexpr std::uint32_t kRenderWorldChunks = 0x800258F0u;
}

psx::cpu::ExecutionResult WorldGuestExecution::resume(Core &core) const {
  return psx::cpu::callOriginal(
      core, kRenderWorldChunks, psx::cpu::ExecutionBudget::currentTurn(core));
}

} // namespace spyro
