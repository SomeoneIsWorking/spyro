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
  // The projection census, counted per whole run rather than per frame: how many eligibility
  // checks got as far as projecting both endpoints, projecting both, and resolving a midpoint.
  // These live here rather than in a function-local static, so they are reset with the rest of
  // the evidence instead of surviving a Core's lifetime.
  uint64_t projected_checks = 0;
  uint64_t resolved_checks = 0;
  uint64_t accepted_checks = 0;
};

// forcedInterpolation is the PSXPORT_FPS60_TFORCE value: 0/1 select an endpoint in
// the extra slot; every other value retains the midpoint, as in Fps60::present_vk.
bool complete(const Evidence &evidence, int forcedInterpolation = -1);
bool proven(const Evidence &evidence);
void finish(Core *core);
bool selftest();

} // namespace spyro::paired_actor::temporal_evidence
