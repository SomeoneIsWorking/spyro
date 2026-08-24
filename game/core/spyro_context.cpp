#include "spyro_context.h"

#include "core.h"

#include <cstdlib>
#include <lucent/log.h>

SpyroContext &spyro_context(Core &core) {
  if (core.gameCtx == nullptr) {
    lucent::error("runtime", "FATAL: per-Core Spyro game context missing");
    std::abort();
  }
  return *static_cast<SpyroContext *>(core.gameCtx);
}

const SpyroContext &spyro_context(const Core &core) {
  if (core.gameCtx == nullptr) {
    lucent::error("runtime", "FATAL: per-Core Spyro game context missing");
    std::abort();
  }
  return *static_cast<const SpyroContext *>(core.gameCtx);
}

SpyroPairedActorFrameState &spyro_paired_actor_state(Core *core) {
  if (core == nullptr) {
    lucent::error("pairedactor", "FATAL: Core missing");
    std::abort();
  }
  return spyro_context(*core).pairedActor;
}
