#include "face_light_environment.h"

#include "core.h"
#include "guest_globals.h"

namespace spyro::face_light {

EnvironmentSource::EnvironmentSource(Core *core) {
  for (std::size_t i = 0; i < magnitude_.size(); ++i) {
    magnitude_[i] = core->mem_r16s(guest::kMagnitudeTable + (std::uint32_t)(i * 2u));
  }
  light_ = {(std::int16_t)core->mem_r32(guest::kLightColorTable + 0x0cu),
            (std::int16_t)core->mem_r32(guest::kLightColorTable + 0x10u),
            (std::int16_t)core->mem_r32(guest::kLightColorTable + 0x14u)};
}

Environment EnvironmentSource::environment() const {
  return Environment{.light = light_, .magnitude = magnitude_};
}

} // namespace spyro::face_light
