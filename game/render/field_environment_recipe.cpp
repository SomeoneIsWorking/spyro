#include "field_environment_recipe.h"

namespace spyro::field_environment {

Invocation derive(State state) {
  if (state.cameraOcclusionGroup < state.occlusionGroupCount) {
    return {.worldSelection = state.cameraOcclusionGroup, .cullingDistance = 0x00028000u};
  }
  const bool titleOrCutscene = state.stage - 13u < 2u;
  return {.worldSelection = -1, .cullingDistance = titleOrCutscene ? 0x0001c000u : 0x00014000u};
}

bool matches(Invocation expected, ObservedBoundary observed) {
  return observed.worldSelection == expected.worldSelection &&
         observed.cullingDistance == expected.cullingDistance && observed.nonzeroWorkBytes == 0u;
}

} // namespace spyro::field_environment
