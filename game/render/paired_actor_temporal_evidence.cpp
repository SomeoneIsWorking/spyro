#include "paired_actor_temporal_evidence.h"

#include "cfg.h"
#include "fx_paired_actor.h"

#include <cstdlib>
#include <lucent/log.h>

bool spyro_paired_temporal_proven(const SpyroPairedTemporalEvidence &evidence) {
  return evidence.eligible_intervals > 0 && evidence.midpoint_calls > 0 &&
         evidence.midpoint_calls == evidence.endpoint_calls &&
         evidence.calls == evidence.midpoint_calls + evidence.endpoint_calls &&
         evidence.emitted == evidence.calls && evidence.no_output == 0;
}

void spyro_paired_actor_temporal_finish(Core *core) {
  const auto &evidence = spyro_paired_actor_state(core).temporal;
  const bool proven = spyro_paired_temporal_proven(evidence);
  const bool observed = evidence.eligible_intervals > 0 || evidence.calls > 0;
  const bool required = cfg_on("PSXPORT_SPYRO_TEMPORAL_VERIFY");
  lucent::info("pairedactor",
               "temporal presenter run proof: eligibility={}/{} midpoint={} endpoint={} "
               "emitted={}/{} no_output={} required={} => {}",
               evidence.eligible_intervals,
               evidence.eligibility_checks,
               evidence.midpoint_calls,
               evidence.endpoint_calls,
               evidence.emitted,
               evidence.calls,
               evidence.no_output,
               required,
               proven     ? "PASS"
               : observed ? "FAIL"
                          : "NOT OBSERVED");
  if (!proven && (required || observed)) {
    lucent::error("pairedactor",
                  "FATAL: paired-actor temporal evidence is incomplete; verification requires an "
                  "eligible interval to traverse one strict-interior and one endpoint callback");
    std::abort();
  }
}

bool spyro_paired_temporal_selftest() {
  SpyroPairedTemporalEvidence evidence{};
  if (spyro_paired_temporal_proven(evidence)) {
    return false;
  }
  evidence.eligibility_checks = 1;
  evidence.eligible_intervals = 1;
  evidence.calls = 2;
  evidence.midpoint_calls = 1;
  evidence.endpoint_calls = 1;
  evidence.emitted = 2;
  if (!spyro_paired_temporal_proven(evidence)) {
    return false;
  }
  evidence.endpoint_calls = 0;
  if (spyro_paired_temporal_proven(evidence)) {
    return false;
  }
  evidence.endpoint_calls = 1;
  evidence.no_output = 1;
  return !spyro_paired_temporal_proven(evidence);
}
