// spyro1_input_phase.cpp — see spyro1_input_phase.h for the key and why it is these two words.

#include "spyro1_input_phase.h"

#include "core.h"
#include "guest_globals.h"

#include <lucent/log.h>

namespace spyro1 {

std::uint64_t InputPhase::of(Core &core) const {
  return pack(core.mem_r32(spyro::guest::kGamestate), core.mem_r32(spyro::guest::kLevelId));
}

std::string InputPhase::describe(std::uint64_t phase) {
  if (phase == psx::input::kUnkeyedPhase) {
    return "unkeyed";
  }
  return lucent::format("gs={}/level={:#x}", gamestateOf(phase), levelOf(phase));
}

} // namespace spyro1
