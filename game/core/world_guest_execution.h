#pragma once

#include "execution_exit.h"

class Core;

namespace spyro {

// Executes the unchanged retail RenderWorldChunks body through Lightrec. The former source-emission
// super-call is intentionally absent.
class WorldGuestExecution {
public:
  psx::cpu::ExecutionResult resume(Core &core) const;
};

} // namespace spyro
