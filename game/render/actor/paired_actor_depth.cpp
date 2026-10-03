#include "paired_actor_depth.h"

#include <algorithm>
#include <cmath>

namespace spyro::paired_actor_depth {
namespace {

double signedWord(double value) {
  value = std::fmod(value, 4294967296.0);
  if (value < 0) {
    value += 4294967296.0;
  }
  return value >= 2147483648.0 ? value - 4294967296.0 : value;
}

Depth deriveSource(double baseZ, uint8_t bias, uint32_t control) {
  const double distance = 512u << (control & 31u);
  return {std::max(0.0, signedWord(baseZ - distance)),
          static_cast<uint32_t>(std::max(0.0, signedWord(std::floor(baseZ / 128.0) - bias))),
          static_cast<uint8_t>((control + 4u) & 31u)};
}

} // namespace

Depth derive(int32_t baseZ, uint8_t bias, uint32_t control) {
  return deriveSource(baseZ, bias, control);
}

std::optional<Depth>
interpolate(int32_t previousZ, int32_t currentZ, uint8_t bias, uint32_t control, float t) {
  if (!std::isfinite(t) || t < 0.0f || t > 1.0f) {
    return std::nullopt;
  }
  return deriveSource(previousZ + (static_cast<double>(currentZ) - previousZ) * t, bias, control);
}

} // namespace spyro::paired_actor_depth
