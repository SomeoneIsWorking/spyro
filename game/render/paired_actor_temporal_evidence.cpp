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
  const bool ordering = a.transform.ot_control == b.transform.ot_control &&
                        a.transform.depth_bias == b.transform.depth_bias &&
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

bool spyro_paired_temporal_complete(const SpyroPairedTemporalEvidence &evidence,
                                    int forcedInterpolation) {
  const bool endpointSlots = forcedInterpolation == 0 || forcedInterpolation == 1;
  const bool paired = endpointSlots
                          ? evidence.midpoint_calls == 0 && evidence.endpoint_calls % 2 == 0
                          : evidence.midpoint_calls == evidence.endpoint_calls;
  return paired && evidence.calls == evidence.midpoint_calls + evidence.endpoint_calls &&
         evidence.emitted + evidence.no_output == evidence.calls &&
         (evidence.calls == 0 || evidence.eligible_intervals > 0);
}

bool spyro_paired_temporal_proven(const SpyroPairedTemporalEvidence &evidence) {
  return spyro_paired_temporal_complete(evidence) && evidence.midpoint_calls > 0 &&
         evidence.emitted > 0;
}

void spyro_paired_actor_temporal_finish(Core *core) {
  const auto &evidence = spyro_paired_actor_state(core).temporal;
  const bool proven = spyro_paired_temporal_proven(evidence);
  const int forcedInterpolation = cfg_int("PSXPORT_FPS60_TFORCE", -1);
  const bool complete = spyro_paired_temporal_complete(evidence, forcedInterpolation);
  const bool required = cfg_on("PSXPORT_SPYRO_TEMPORAL_VERIFY");
  lucent::info("pairedactor",
               "temporal presenter run proof: eligibility={}/{} midpoint={} endpoint={} "
               "emitted={}/{} no_output={} tforce={} required={} => {}",
               evidence.eligible_intervals,
               evidence.eligibility_checks,
               evidence.midpoint_calls,
               evidence.endpoint_calls,
               evidence.emitted,
               evidence.calls,
               evidence.no_output,
               forcedInterpolation,
               required,
               proven      ? "PASS"
               : !complete ? "INCOMPLETE"
                           : "VISIBLE INTERPOLATION NOT OBSERVED");
  if (!complete || (required && !proven)) {
    lucent::error(
        "pairedactor",
        "FATAL: paired-actor temporal evidence rejected: complete={} visible={} required={}; "
        "callbacks must be paired and accounted for; required verification also needs emission",
        complete,
        proven,
        required);
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
  if (spyro_paired_temporal_proven(evidence)) {
    return false; // double-counting a callback cannot pass
  }
  evidence.emitted = 1;
  if (!spyro_paired_temporal_proven(evidence)) {
    return false; // a visibility transition can have a complete, empty endpoint
  }
  evidence.no_output = 0;
  if (spyro_paired_temporal_proven(evidence)) {
    return false; // an unaccounted callback cannot pass
  }
  evidence.emitted = 0;
  evidence.no_output = 2;
  return spyro_paired_temporal_complete(evidence) &&
         !spyro_paired_temporal_proven(evidence); // all-empty is valid but proves no visible motion
}
