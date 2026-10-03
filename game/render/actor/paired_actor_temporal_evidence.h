#pragma once

#include <cstdint>

class Core;

namespace spyro::paired_actor::temporal_evidence {

// Whole-run evidence written only by the shipping paired-actor eligibility check and
// Fps60 source reconstruction callback. The run-end verifier rejects partial presenter sequences.
struct Evidence {
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
bool complete(const Evidence &evidence, int forcedInterpolation = -1);
bool proven(const Evidence &evidence);
void finish(Core *core);
bool selftest();

} // namespace spyro::paired_actor::temporal_evidence
