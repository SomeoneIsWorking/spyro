#pragma once

#include "guest_globals.h"

#include <compare>
#include <cstdint>

namespace spyro::field_environment {

// Persistent game state consumed by FIELD layer 0x8002B9CC. The shared globals it reads live in
// spyro::guest; only the addresses this layer alone touches are named here.
constexpr uint32_t kCullingDistance = guest::kEnvironment + 0x28u;
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
