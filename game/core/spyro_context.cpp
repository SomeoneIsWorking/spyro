#include "spyro_context.h"

#include "core.h"

#include <cstdlib>
#include <lucent/log.h>

spyro::Context &spyro::context(Core &core) {
  if (core.gameCtx == nullptr) {
    lucent::error("runtime", "FATAL: per-Core Spyro game context missing");
    std::abort();
  }
  return *static_cast<spyro::Context *>(core.gameCtx);
}

const spyro::Context &spyro::context(const Core &core) {
  if (core.gameCtx == nullptr) {
    lucent::error("runtime", "FATAL: per-Core Spyro game context missing");
    std::abort();
  }
  return *static_cast<const spyro::Context *>(core.gameCtx);
}

spyro::paired_actor::FrameState &spyro::paired_actor::state(Core *core) {
  if (core == nullptr) {
    lucent::error("pairedactor", "FATAL: Core missing");
    std::abort();
  }
  return spyro::context(*core).pairedActor;
}
