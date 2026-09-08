#include "runtime_run.h"

#include "core.h"
#include "game.h"
#include "lightrec_executor.h"
#include "spyro_context.h"

#include <lucent/log.h>

namespace spyro {
RuntimeRun &runtimeRun(Core &core) {
  return spyro_context(core).run;
}

void reportRuntimeRun(Core &core, std::uint64_t completedSteps) {
  const auto &counts = core.lightrecExecutor().counters();
  lucent::info("runtime",
               "run complete: fields={} product_steps={} presentation_fences={} "
               "translated_blocks={} executed_blocks={} executed_instructions={} "
               "cache_hits={} cache_misses={} host_dispatches={} invalidations={} faults={}",
               runtimeRun(core).fields(),
               completedSteps,
               core.game->presentation.fence(),
               counts.translatedBlocks,
               counts.executedBlocks,
               counts.executedInstructions,
               counts.cacheHits,
               counts.cacheMisses,
               counts.hostDispatches,
               counts.invalidations,
               counts.faults);
  core.lightrecExecutor().reportFallbackTelemetry("run-complete");
}
} // namespace spyro
