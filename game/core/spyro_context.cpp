#include "spyro_context.h"

#include "core.h"

#include <cstdlib>
#include <lucent/log.h>

SpyroPairedActorFrameState &spyro_paired_actor_state(Core *core) {
  if (!core || !core->gameCtx) {
    lucent::error("pairedactor", "FATAL: per-Core Spyro game context missing");
    std::abort();
  }
  return static_cast<SpyroContext *>(core->gameCtx)->pairedActor;
}
