#pragma once

#include <cstdint>

class Core;

namespace spyro::world_draw {

// Direct semantic producer for Spyro's RenderWorldChunks (0x800258F0).
// Returns false without mutating guest visibility or the render queue when the
// whole static-world recipe cannot be represented exactly.
bool submit(Core *core, int32_t selection);

} // namespace spyro::world_draw
