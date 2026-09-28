#include "runtime_run.h"

#include "core.h"
#include "game.h"
#include "lightrec_executor.h"
#include "paired_actor_temporal_evidence.h"
#include "render_stats.h" // render_depth_coverage_report — instrument I051
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
  // Depth coverage over the WHOLE run. This call site was lost when producer_run.cpp was removed,
  // so instrument I051 could not print at all; it is restored here, and with it restored the
  // instrument says out loud what the missing call site was hiding:
  //
  //   [ndepth:warn] depth coverage (run-complete): NO PRIMITIVES WERE CLASSIFIED AT ALL this run
  //
  // on a run that reached Artisans and presented 3,606 frames. The counters it reports are
  // incremented inside the framework's GUEST-OT classifier in gpu_native.cpp, which a
  // native-producer run never executes, so they are zero wherever this call is placed. The call
  // stays because a report that states "nothing measured" in prose is strictly better than silence
  // — that is exactly the distinction tools/depth_cov.py failed to make when it reported
  // "59 sampled frames, 0 carrying primitives" and meant "I never ran". Do NOT read a future
  // non-zero from here as native-path coverage without first checking which classifier moved it;
  // measuring this port's coverage means counting where its producers SET depth, at RqItem::depth
  // on submission, and nothing does that yet (see docs/info/instruments/051-*.md).
  render_depth_coverage_report(&core, "run-complete");
  // WHO DREW THIS RUN, per producer. The census has been FED since `render_submission.cpp` started
  // calling `noteNativeLayer`, but nothing ever printed it, so the one question the census exists to
  // answer -- "which producer owns the thing I am looking at" -- had no answer even with every number
  // already collected. It was asked for directly when a frame showed the pool water rendering as
  // per-block colour noise while the actors, hedges and buildings were clean: the census is the
  // instrument for exactly that, and it was running and mute.
  //
  // `report()` REFUSES LOUDLY when it was never fed, so a future zero cannot read as "no producer
  // drew anything" -- which is the failure `depth_cov.py` made when it reported zero for a run whose
  // path never executes its counters.
  core.rsub.census.report("spyro run-complete");
  spyro_paired_actor_temporal_finish(&core);
}
} // namespace spyro
