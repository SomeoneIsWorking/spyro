#pragma once

#include "native_projection.h"

#include <optional>

namespace spyro {

// A short-lived projection over one authored transform, or between two endpoints of it. It never
// owns history, projected recipes or live guest state.
//
// Interpolation samples matching source transforms in view space: each endpoint's own vertex
// through its own matrix, before projection. Two projected vertices cannot be interpolated, because
// they are saturated and wrapped and no rotation can be recovered from them.
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
  // The sample position, or nothing in endpoint mode. A caller that must sample a scalar alongside
  // the geometry — an actor's depth origin, say — reads it here instead of carrying its own copy.
  const std::optional<double> &at() const {
    return t_;
  }

private:
  psxport::native_projection::FixedAffine previous_{};
  psxport::native_projection::FixedAffine current_{};
  psxport::native_projection::ProjectionParams projection_{};
  std::optional<double> t_;
};

} // namespace spyro
