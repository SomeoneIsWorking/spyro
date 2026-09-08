#include "paired_actor_temporal_evidence.h"

#include "cfg.h"
#include "fx_paired_actor.h"

#include <cstdlib>
#include <lucent/log.h>

void spyro_paired_actor_log_frame_compatibility(const SpyroPairedFrame &a,
                                                const SpyroPairedFrame &b,
                                                bool compatible) {
  static uint64_t scanned = 0, matched = 0;
  ++scanned;
  matched += compatible;
  const bool identity = a.valid && b.valid && !a.culled && !b.culled && a.epoch == b.epoch;
  const bool topology = a.topology == b.topology && a.layer_counts == b.layer_counts &&
                        a.primitives.size() == b.primitives.size();
  const bool materials = a.materials == b.materials && a.override_control == b.override_control;
  const bool projection = a.transform.ofx == b.transform.ofx &&
                          a.transform.ofy == b.transform.ofy && a.transform.h == b.transform.h;
  const bool ordering = a.transform.depth_origin == b.transform.depth_origin &&
                        a.transform.ot_shift == b.transform.ot_shift;
  const bool gpu = a.gpu.da_x0 - a.gpu.off_x == b.gpu.da_x0 - b.gpu.off_x &&
                   a.gpu.da_y0 - a.gpu.off_y == b.gpu.da_y0 - b.gpu.off_y &&
                   a.gpu.da_x1 - a.gpu.off_x == b.gpu.da_x1 - b.gpu.off_x &&
                   a.gpu.da_y1 - a.gpu.off_y == b.gpu.da_y1 - b.gpu.off_y &&
                   a.gpu.tw_mx == b.gpu.tw_mx && a.gpu.tw_my == b.gpu.tw_my &&
                   a.gpu.tw_ox == b.gpu.tw_ox && a.gpu.tw_oy == b.gpu.tw_oy;
  lucent::debug("pairedactor",
                "temporal recipe census: scanned={} matched={} identity={} topology={} "
                "materials={} projection={} ordering={} gpu={} prev_faces={} cur_faces={}",
                scanned,
                matched,
                identity,
                topology,
                materials,
                projection,
                ordering,
                gpu,
                a.primitives.size(),
                b.primitives.size());
}

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
