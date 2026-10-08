#pragma once

#include "cyclorama_portal_mesh_recipe.h"
#include "guest_globals.h"
#include "sector_visibility.h"

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
// Retail 0x80050EA8 reads a portal's sector byte from D_800771C8; this decides what the port DRAWS,
// so it takes the drawn table the world submission published beside that guest table (issue 0152).
Recipe prepare(Core *core, const sector_visibility::Table &drawnSectors);
void publishSpin(Core *core, const Recipe &recipe);
Status classifyPortalFrame(const cyclorama_portal_mesh::PortalFrame &frame);
const char *statusName(Status status);

} // namespace spyro::cyclorama_scene_recipe
