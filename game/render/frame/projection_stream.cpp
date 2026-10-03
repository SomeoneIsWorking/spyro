#include "projection_stream.h"

namespace spyro {

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

} // namespace spyro
