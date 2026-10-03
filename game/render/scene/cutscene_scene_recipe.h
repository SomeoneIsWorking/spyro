#pragma once

#include <cstdint>

class Core;

namespace spyro::cutscene_scene_recipe {

constexpr uint32_t kWorldLowDetailFarLimit = 0x800785D0u;

struct State {
  uint8_t backgroundR = 0;
  uint8_t backgroundG = 0;
  uint8_t backgroundB = 0;
  uint32_t fade = 0;
};

struct WorldInvocation {
  int32_t worldSelection = -1;
  uint32_t lowDetailFarLimit = 0;
};

// Resident handler 0x8001E9C8 copies the cyclorama clear colour into both
// DRAWENVs before the driver programs the GPU. Read and apply that render-only
// state before nativeFrameBegin so its background fill sees the authored colour.
State read(Core *core);
void prepareFrame(Core *core, const State &state);

// The same handler writes 0x14000 immediately before RenderWorldChunks(-1).
WorldInvocation worldInvocation();
void applyWorldInvocation(Core *core, const WorldInvocation &invocation);

} // namespace spyro::cutscene_scene_recipe
