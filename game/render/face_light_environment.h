#pragma once

#include "face_light_program.h"

#include <array>

class Core;

namespace spyro::face_light {

// Owns the snapshot of guest globals the per-face colour program reads: the light colour at
// D_800770C8 and the reciprocal-magnitude table at 0x80074B84. Retail re-reads both inside its
// face loop, and nothing in that loop writes them, so one snapshot per composition is the same
// data with none of the per-face traffic. The Environment it hands out borrows this object's
// storage and must not outlive it.
class EnvironmentSource {
public:
  explicit EnvironmentSource(Core *core);

  Environment environment() const;

private:
  std::array<std::int16_t, kMagnitudeEntries> magnitude_{};
  LightColor light_{};
};

} // namespace spyro::face_light
