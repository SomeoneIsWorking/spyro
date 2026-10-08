#pragma once

#include <array>
#include <cstdint>

class Core;

namespace spyro::render {

// The terrain producer's guest-facing entry points (0x8004EBA8): a selector and two `SHORTMATRIX`
// pointers. The producer itself is three owners — `terrain_scene` reads the corpus out of guest
// memory, `terrain_recipe` projects it, `terrain_emit` preflights and publishes it.
bool submitTerrainGuest(Core *core,
                        std::int32_t selector,
                        std::uint32_t cullMatrix,
                        std::uint32_t viewMatrix);

// The same producer for a caller that BUILT its matrices rather than finding them in guest RAM.
// `func_8001A050` does exactly that while a level entrance sweep is still winding down.
bool submitTerrainGuestWithMatrices(Core *core,
                                    std::int32_t selector,
                                    const std::array<std::uint32_t, 5> &cull,
                                    const std::array<std::uint32_t, 5> &view);

} // namespace spyro::render
