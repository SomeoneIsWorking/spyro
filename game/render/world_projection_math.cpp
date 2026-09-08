#include "world_projection_math.h"

#include <cstdint>

namespace spyro::world_projection_math {

ProjectionStream::ProjectionStream(const psxport::native_projection::FixedAffine &endpoint,
                                   const psxport::native_projection::ProjectionParams &projection)
    : previous_(endpoint), current_(endpoint), projection_(projection) {}

ProjectionStream::ProjectionStream(const psxport::native_projection::FixedAffine &previous,
                                   const psxport::native_projection::FixedAffine &current,
                                   const psxport::native_projection::ProjectionParams &projection,
                                   double t)
    : previous_(previous), current_(current), projection_(projection), t_(t) {}

std::optional<psxport::native_projection::NativeProjectedVertex>
ProjectionStream::project(psxport::native_projection::ModelVertex previous,
                          psxport::native_projection::ModelVertex current) const {
  const auto currentView = psxport::native_projection::transform(current_, current);
  if (!t_) {
    return psxport::native_projection::project_transformed(currentView, projection_);
  }
  return psxport::native_projection::sample_view(
      psxport::native_projection::transform(previous_, previous), currentView, projection_, *t_);
}

psxport::native_projection::FixedAffine decodeMatrix(const world_chunk_codec::RamView &ram,
                                                     uint32_t address) {
  const uint32_t w0 = ram.r32(address);
  const uint32_t w1 = ram.r32(address + 4u);
  const uint32_t w2 = ram.r32(address + 8u);
  const uint32_t w3 = ram.r32(address + 12u);
  const uint32_t w4 = ram.r32(address + 16u);
  psxport::native_projection::FixedAffine out{};
  out.m = {{{(int16_t)w0, (int16_t)(w0 >> 16), (int16_t)w1},
            {(int16_t)(w1 >> 16), (int16_t)w2, (int16_t)(w2 >> 16)},
            {(int16_t)w3, (int16_t)(w3 >> 16), (int16_t)w4}}};
  return out;
}

psxport::native_projection::ModelVertex packProjectionInput(int32_t x, int32_t y, int32_t z) {
  const uint32_t packedXy = (uint32_t)x + ((uint32_t)y << 16);
  return {(int16_t)packedXy, (int16_t)(packedXy >> 16), (int16_t)z};
}

int32_t
nclip(const world_recipe::Vertex &a, const world_recipe::Vertex &b, const world_recipe::Vertex &c) {
  const int64_t value = (int64_t)a.sx * b.sy + (int64_t)b.sx * c.sy + (int64_t)c.sx * a.sy -
                        (int64_t)a.sx * c.sy - (int64_t)b.sx * a.sy - (int64_t)c.sx * b.sy;
  return (int32_t)(uint32_t)value;
}

} // namespace spyro::world_projection_math
