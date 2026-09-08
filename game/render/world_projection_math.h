#pragma once

#include "native_projection.h"
#include "world_chunk_codec.h"
#include "world_recipe.h"

#include <cstdint>

namespace spyro::world_projection_math {

psxport::native_projection::FixedAffine decodeMatrix(const world_chunk_codec::RamView &ram,
                                                     uint32_t address);

// A short-lived projection stream over authored endpoint transforms. It never owns history,
// projected recipes or live guest state. Endpoint mode retains MAC overflow behavior; interior
// mode propagates sample_view's refusal instead of silently dropping a vertex.
class ProjectionStream {
public:
  ProjectionStream(const psxport::native_projection::FixedAffine &endpoint,
                   const psxport::native_projection::ProjectionParams &projection);
  ProjectionStream(const psxport::native_projection::FixedAffine &previous,
                   const psxport::native_projection::FixedAffine &current,
                   const psxport::native_projection::ProjectionParams &projection,
                   double t);
  std::optional<psxport::native_projection::NativeProjectedVertex>
  project(psxport::native_projection::ModelVertex previous,
          psxport::native_projection::ModelVertex current) const;
  const psxport::native_projection::FixedAffine &currentMatrix() const {
    return current_;
  }
  const psxport::native_projection::ProjectionParams &parameters() const {
    return projection_;
  }

private:
  psxport::native_projection::FixedAffine previous_{};
  psxport::native_projection::FixedAffine current_{};
  psxport::native_projection::ProjectionParams projection_{};
  std::optional<double> t_;
};

// The guest loads GTE VXY with one 32-bit `x + (y << 16)` operation. A
// negative low-half x therefore borrows into y; narrowing the coordinates
// independently is observably different at projection rounding boundaries.
psxport::native_projection::ModelVertex packProjectionInput(int32_t x, int32_t y, int32_t z);

// Exact wrapped NCLIP determinant used by every RenderWorldChunks face path.
int32_t
nclip(const world_recipe::Vertex &a, const world_recipe::Vertex &b, const world_recipe::Vertex &c);

} // namespace spyro::world_projection_math
