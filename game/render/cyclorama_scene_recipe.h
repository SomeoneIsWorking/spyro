#pragma once

#include "cyclorama_portal_mesh_recipe.h"

#include <cstdint>
#include <vector>

class Core;

namespace spyro::cyclorama_scene_recipe {

// Persistent inputs and side effects of the resident cyclorama owner
// 0x80050BD0. Portal drawing is a separate, still-unowned renderer family;
// this recipe accepts inactive portals and active records whose projected
// aperture is empty before invoking the owned static-mesh producer 0x8004EBA8.
constexpr uint32_t kSpinYaw = 0x80075858u;
constexpr uint32_t kSpinPitch = 0x800758fcu;
constexpr uint32_t kPortalCount = 0x800758bcu;
constexpr uint32_t kPortals = 0x80078640u;
constexpr uint32_t kBroadVisibility = 0x800771c8u;
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kCameraOcclusionGroup = kCamera + 0x54u;
constexpr uint32_t kEnvironment = 0x800785a8u;
constexpr uint32_t kEnvironmentOcclusionGroupCount = kEnvironment + 0x0cu;
constexpr uint32_t kPortalCapacity = 6u;

enum class Status : uint8_t {
  Ready,
  InvalidCore,
  InvalidPortalCount,
  InvalidPortalPointer,
  InvalidPortalSector,
  InvalidPortalRecipe,
  ActivePortalDrawUnsupported,
};

struct Recipe {
  Status status = Status::InvalidCore;
  int32_t mainSelection = -1;
  uint32_t nextYaw = 0;
  int32_t nextPitch = 0;
  int32_t portalCount = 0;
  uint32_t activePortals = 0;
  uint32_t validEmptyPortals = 0;
  std::vector<cyclorama_portal_mesh::PortalFrame> portalFrames;
  const char *refusal = "invalid_core";
};

// Pure/read-only preparation. No spin state or render queue is published until
// the complete supported recipe has passed the downstream terrain admission.
Recipe prepare(Core *core);
void publishSpin(Core *core, const Recipe &recipe);
Status classifyPortalFrame(const cyclorama_portal_mesh::PortalFrame &frame);
const char *statusName(Status status);

} // namespace spyro::cyclorama_scene_recipe
