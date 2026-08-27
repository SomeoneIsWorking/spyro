#include "cutscene_scene_recipe.h"

#include "core.h"

namespace spyro::cutscene_scene_recipe {
namespace {

// 0x8001E9C8 — generated/shard_4.c plus the matching Rosetta body in
// external/spyro-1/src/gamestates/draw.c. Focused recipe tests verify the render-only state
// transcription; isolated real-disc runtime and visual evidence is C228 / issue 0088.
constexpr uint32_t kCycloramaBackground = 0x80078A50u;
constexpr uint32_t kFade = 0x80075918u;
constexpr uint32_t kDrawEnvA = 0x80076EE0u;
constexpr uint32_t kDrawEnvB = 0x80076F64u;
constexpr uint32_t kDrawEnvBackground = 0x19u;
constexpr uint32_t kCutsceneLowDetailFarLimit = 0x00014000u;

void writeBackground(Core *core, uint32_t drawEnv, const State &state) {
  core->mem_w8(drawEnv + kDrawEnvBackground + 0u, state.backgroundR);
  core->mem_w8(drawEnv + kDrawEnvBackground + 1u, state.backgroundG);
  core->mem_w8(drawEnv + kDrawEnvBackground + 2u, state.backgroundB);
}

} // namespace

State read(Core *core) {
  return {.backgroundR = core->mem_r8(kCycloramaBackground + 0u),
          .backgroundG = core->mem_r8(kCycloramaBackground + 1u),
          .backgroundB = core->mem_r8(kCycloramaBackground + 2u),
          .fade = core->mem_r32(kFade)};
}

void prepareFrame(Core *core, const State &state) {
  writeBackground(core, kDrawEnvA, state);
  writeBackground(core, kDrawEnvB, state);
}

WorldInvocation worldInvocation() {
  return {.worldSelection = -1, .lowDetailFarLimit = kCutsceneLowDetailFarLimit};
}

void applyWorldInvocation(Core *core, const WorldInvocation &invocation) {
  core->mem_w32(kWorldLowDetailFarLimit, invocation.lowDetailFarLimit);
}

} // namespace spyro::cutscene_scene_recipe
