#pragma once

#include <array>
#include <cstdint>

namespace spyro {

// Camera inputs before producer-specific coordinate packing, shifts or actor rotation.
// Captured from the locals used by each producer, never inferred from projected vertices.
struct SceneCameraInputs {
  bool valid = false;
  std::array<std::array<int16_t, 3>, 3> matrix{};
  std::array<int32_t, 3> position{};
  bool operator==(const SceneCameraInputs &) const = default;
};

} // namespace spyro
