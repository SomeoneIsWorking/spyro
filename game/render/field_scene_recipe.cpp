#include "field_scene_recipe.h"

#include "core.h"

namespace spyro::field_scene_recipe {
namespace {

constexpr uint32_t kCycloramaBackground = 0x80078a50u;
constexpr uint32_t kFade = 0x80075918u;
constexpr uint32_t kFlightLevel = 0x80075690u;
constexpr uint32_t kPlayerHidden = 0x80075814u;
constexpr uint32_t kDrawEnvA = 0x80076ee0u;
constexpr uint32_t kDrawEnvB = 0x80076f64u;
constexpr uint32_t kDrawEnvBackground = 0x19u;

void writeBackground(Core *core, uint32_t drawEnv, const State &state) {
  core->mem_w8(drawEnv + kDrawEnvBackground + 0u, state.backgroundR);
  core->mem_w8(drawEnv + kDrawEnvBackground + 1u, state.backgroundG);
  core->mem_w8(drawEnv + kDrawEnvBackground + 2u, state.backgroundB);
}

} // namespace

State read(Core *core) {
  return {.environment = {.cameraOcclusionGroup =
                              (int32_t)core->mem_r32(field_environment::kCameraOcclusionGroup),
                          .occlusionGroupCount =
                              (int32_t)core->mem_r32(field_environment::kOcclusionGroupCount),
                          .stage = core->mem_r32(field_environment::kStageSelector)},
          .backgroundR = core->mem_r8(kCycloramaBackground + 0u),
          .backgroundG = core->mem_r8(kCycloramaBackground + 1u),
          .backgroundB = core->mem_r8(kCycloramaBackground + 2u),
          .fade = core->mem_r32(kFade),
          .flightLevel = core->mem_r32(kFlightLevel) != 0u,
          .playerVisible = core->mem_r32(kPlayerHidden) == 0u};
}

Plan derive(const State &state) {
  Plan plan{.environment = field_environment::derive(state.environment)};
  plan.layers[plan.layerCount++] = Layer::MobyListBuild;
  if (!state.flightLevel) {
    plan.layers[plan.layerCount++] = Layer::Collectables;
  }
  plan.layers[plan.layerCount++] = Layer::RegularActors;
  if (state.playerVisible) {
    plan.layers[plan.layerCount++] = Layer::PlayerActor;
  }
  plan.layers[plan.layerCount++] = Layer::Environment;
  plan.layers[plan.layerCount++] = Layer::Cyclorama;
  if (state.fade != 0u) {
    plan.layers[plan.layerCount++] = Layer::Fade;
  }
  return plan;
}

void prepareFrame(Core *core, const State &state) {
  writeBackground(core, kDrawEnvA, state);
  writeBackground(core, kDrawEnvB, state);
}

void applyEnvironment(Core *core, field_environment::Invocation invocation) {
  for (uint32_t i = 0; i < field_environment::kEdgeWorkAreaSize; ++i) {
    core->mem_w8(field_environment::kEdgeWorkArea + i, 0u);
  }
  core->mem_w32(field_environment::kCullingDistance, invocation.cullingDistance);
}

void applyEnvironment(Core *core, const Plan &plan) {
  applyEnvironment(core, plan.environment);
}

} // namespace spyro::field_scene_recipe
