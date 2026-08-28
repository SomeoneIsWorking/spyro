#include "spyro1_transition_skip.h"

#include "core.h"

#include <cstdint>
#include <lucent/log.h>

namespace spyro1 {
namespace {

constexpr std::uint32_t kGamestate = 0x800757D8u;
constexpr std::uint32_t kLoadStage = 0x80075864u;
constexpr std::uint32_t kTransitionTicks = 0x800756ACu;
constexpr std::uint32_t kTransitionHudActive = 0x800756B0u;
constexpr std::uint32_t kTransitionEndTick = 417u; // guest hides the HUD when ticks > 416

} // namespace

bool skipLevelTransition(Core &core, bool startEdge) {
  if (!startEdge || core.mem_r32(kGamestate) != 1u || core.mem_r32(kTransitionHudActive) == 0u ||
      core.mem_r32(kLoadStage) == 0xffffffffu) {
    return false;
  }

  const std::uint32_t before = core.mem_r32(kTransitionTicks);
  core.mem_w32(kTransitionTicks, kTransitionEndTick);
  core.mem_w32(kTransitionHudActive, 0u);
  lucent::info("skips",
               "Start skipped level-transition tally: g_LevelTransTicks {} -> {} (CD loading "
               "and guest transition state remain active)",
               before,
               kTransitionEndTick);
  return true;
}

} // namespace spyro1
