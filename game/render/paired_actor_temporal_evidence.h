#pragma once

#include <cstdint>

class Core;

// Whole-run evidence written only by the shipping paired-actor eligibility check and
// Fps60 source reconstruction callback. The run-end verifier rejects partial presenter sequences.
struct SpyroPairedTemporalEvidence {
  uint64_t calls = 0;
  uint64_t midpoint_calls = 0;
  uint64_t endpoint_calls = 0;
  uint64_t emitted = 0;
  uint64_t no_output = 0;
  uint64_t eligibility_checks = 0;
  uint64_t eligible_intervals = 0;
};

// forcedInterpolation is the PSXPORT_FPS60_TFORCE value: 0/1 select an endpoint in
// the extra slot; every other value retains the midpoint, as in Fps60::present_vk.
bool spyro_paired_temporal_complete(const SpyroPairedTemporalEvidence &evidence,
                                    int forcedInterpolation = -1);
bool spyro_paired_temporal_proven(const SpyroPairedTemporalEvidence &evidence);
void spyro_paired_actor_temporal_finish(Core *core);
bool spyro_paired_temporal_selftest();
