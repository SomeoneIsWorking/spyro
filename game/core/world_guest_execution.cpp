#include "world_guest_execution.h"

#include "guest_execution.h"

namespace spyro {
namespace {
constexpr std::uint32_t kRenderWorldChunks = 0x800258F0u;
}

psx::cpu::ExecutionResult WorldGuestExecution::resume(Core &core) const {
  return GuestExecution(core).callOriginal(kRenderWorldChunks);
}

} // namespace spyro
