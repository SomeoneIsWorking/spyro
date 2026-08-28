#pragma once

#include "field_environment_recipe.h"

#include <array>
#include <cstdint>

class Core;

namespace spyro::field_scene_recipe {

enum class Layer : uint8_t {
  MobyListBuild,
  Collectables,
  RegularActors,
  PlayerActor,
  Environment,
  Cyclorama,
  Fade
};

struct State {
  field_environment::State environment{};
  uint8_t backgroundR = 0;
  uint8_t backgroundG = 0;
  uint8_t backgroundB = 0;
  uint32_t fade = 0;
  bool flightLevel = false;
  bool playerVisible = false;
};

struct Plan {
  field_environment::Invocation environment{};
  std::array<Layer, 7> layers{};
  uint32_t layerCount = 0;
};

// Render-only state and authored producer order from the stage-0 arm of
// SCUS_942.28 0x8001ED5C. The plan contains only source-owned native
// producers; each submitter retains its own atomic refusal boundary.
State read(Core *core);
Plan derive(const State &state);
void prepareFrame(Core *core, const State &state);
void applyEnvironment(Core *core, field_environment::Invocation invocation);
void applyEnvironment(Core *core, const Plan &plan);

} // namespace spyro::field_scene_recipe
