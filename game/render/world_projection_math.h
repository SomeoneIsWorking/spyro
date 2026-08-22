#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"
#include "world_recipe.h"

#include <cstdint>

namespace spyro::world_projection_math {

psxport::native_projection::FixedAffine decodeMatrix(const world_chunk_codec::RamView &ram,
                                                     uint32_t address);

// The guest loads GTE VXY with one 32-bit `x + (y << 16)` operation. A
// negative low-half x therefore borrows into y; narrowing the coordinates
// independently is observably different at projection rounding boundaries.
psxport::native_projection::ModelVertex packProjectionInput(int32_t x, int32_t y, int32_t z);

// Exact wrapped NCLIP determinant used by every RenderWorldChunks face path.
int32_t
nclip(const world_recipe::Vertex &a, const world_recipe::Vertex &b, const world_recipe::Vertex &c);

} // namespace spyro::world_projection_math
