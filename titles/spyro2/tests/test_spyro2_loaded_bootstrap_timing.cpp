#include "spyro2_loaded_bootstrap_timing.h"

#include <array>
#include <cstdint>

namespace {

using Phase = spyro2::Spyro2LoadedBootstrapTiming::Phase;
using Transition = spyro2::LoadedBootstrapTransition;

struct ExpectedRun {
  std::uint32_t callsite;
  std::uint32_t fields;
  Transition transition;
  Phase next;
};

constexpr std::array kExpectedRuns{
    ExpectedRun{0x800772F4u, 12u, Transition::ChildAComplete, Phase::Threshold164},
    ExpectedRun{0x80077504u, 152u, Transition::Threshold164Complete, Phase::ClearA},
    ExpectedRun{0x8004C494u, 1u, Transition::ClearAComplete, Phase::FixedA},
    ExpectedRun{0x80077524u, 4u, Transition::FixedAComplete, Phase::ChildB},
    ExpectedRun{0x800772F4u, 12u, Transition::ChildBComplete, Phase::Threshold60},
    ExpectedRun{0x8007769Cu, 48u, Transition::Threshold60Complete, Phase::ChildC},
    ExpectedRun{0x800772F4u, 12u, Transition::ChildCComplete, Phase::Threshold180A},
    ExpectedRun{0x800778E4u, 108u, Transition::Threshold180AComplete, Phase::ClearB},
    ExpectedRun{0x8004C494u, 1u, Transition::ClearBComplete, Phase::FixedB},
    ExpectedRun{0x80077944u, 4u, Transition::FixedBComplete, Phase::ChildD},
    ExpectedRun{0x800772F4u, 12u, Transition::ChildDComplete, Phase::Threshold180B},
    ExpectedRun{0x80077B98u, 168u, Transition::Threshold180BComplete, Phase::Complete},
};

} // namespace

int main() {
  spyro2::Spyro2LoadedBootstrapTiming timing;
  if (timing.begun() || timing.complete() || timing.consumeField(1u) != Transition::Invalid) {
    return 1;
  }

  // Starting near wrap proves the retail unsigned subtraction semantics, not only the zero-origin
  // case. The first child consumes 12 of the 164 fields since the snapshot, so 0x80077504 owns the
  // remaining 152 fields rather than an independently invented 164-field delay.
  std::uint32_t field = 0xFFFFFF80u;
  std::uint32_t fieldsDelivered = 0u;
  timing.begin(field);
  for (const ExpectedRun &run : kExpectedRuns) {
    if (timing.nextFieldCallsite() != run.callsite) {
      return 2;
    }
    for (std::uint32_t i = 0; i < run.fields; ++i) {
      ++field;
      ++fieldsDelivered;
      const Transition transition = timing.consumeField(field);
      const Transition expected = i + 1u == run.fields ? run.transition : Transition::None;
      if (transition != expected) {
        return 3;
      }
    }
    if (timing.phase() != run.next) {
      return 4;
    }
  }

  if (!timing.complete() || timing.nextFieldCallsite() != 0u || fieldsDelivered != 534u ||
      timing.consumeField(field + 1u) != Transition::Invalid) {
    return 5;
  }
  return 0;
}
