#pragma once

#include <compare>
#include <cstdint>

namespace spyro::field_environment {

// Persistent game state consumed by FIELD layer 0x8002B9CC. The addresses are
// named once here so the shipping owner and the retail-call oracle cannot
// silently drift onto different fields.
constexpr uint32_t kStageSelector = 0x800757d8u;
constexpr uint32_t kCameraOcclusionGroup = 0x80076e24u;
constexpr uint32_t kEnvironment = 0x800785a8u;
constexpr uint32_t kOcclusionGroupCount = kEnvironment + 0x0cu;
constexpr uint32_t kCullingDistance = kEnvironment + 0x28u;
constexpr uint32_t kEdgeWorkArea = 0x8006fcf4u;
constexpr uint32_t kEdgeWorkAreaSize = 0x1c00u;

struct State {
  int32_t cameraOcclusionGroup = 0;
  int32_t occlusionGroupCount = 0;
  uint32_t stage = 0;
};

struct Invocation {
  int32_t worldSelection = -1;
  uint32_t cullingDistance = 0;

  auto operator<=>(const Invocation &) const = default;
};

struct ObservedBoundary {
  int32_t worldSelection = -1;
  uint32_t cullingDistance = 0;
  uint32_t nonzeroWorkBytes = 0;
};

// Exact branch contract decompiled from SCUS_942.28 0x8002B9CC. This is a
// pure seam because the future shipping FIELD owner and the diagnostic retail
// call comparison must exercise one implementation of the selection rule.
Invocation derive(State state);
bool matches(Invocation expected, ObservedBoundary observed);

} // namespace spyro::field_environment
