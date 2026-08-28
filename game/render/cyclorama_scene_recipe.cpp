#include "cyclorama_scene_recipe.h"

#include "core.h"
#include "world_chunk_codec.h"

#include <span>

namespace spyro::cyclorama_scene_recipe {
namespace {

constexpr uint32_t kPortalWorldSector = 0x14u;

Recipe refuse(Recipe recipe, Status status, const char *why) {
  recipe.status = status;
  recipe.refusal = why;
  return recipe;
}

int32_t advancePitch(uint32_t pitch) {
  int32_t advanced = (int32_t)((pitch + 2u) & 0xfffu);
  if (advanced >= 0x801) {
    advanced -= 0x1000;
  }
  if (advanced >= 0x81) {
    advanced = 0x80;
  }
  return advanced;
}

} // namespace

Status classifyPortalFrame(const cyclorama_portal_mesh::PortalFrame &frame) {
  // The portal mask family runs for every on-screen aperture, even when the
  // distance-selected mesh family is empty. Do not let a far portal's
  // ValidEmpty mesh result suppress its visible mask.
  if (frame.maskVisible) {
    if (frame.status == cyclorama_portal_mesh::Status::ValidEmpty ||
        frame.status == cyclorama_portal_mesh::Status::Ready ||
        frame.status == cyclorama_portal_mesh::Status::NearFamilyUnsupported) {
      return Status::Ready;
    }
    return Status::InvalidPortalRecipe;
  }
  if (frame.status == cyclorama_portal_mesh::Status::ValidEmpty) {
    return Status::Ready;
  }
  return Status::InvalidPortalRecipe;
}

Recipe prepare(Core *core) {
  Recipe recipe{};
  if (core == nullptr) {
    return recipe;
  }

  recipe.nextYaw = (core->mem_r32(kSpinYaw) + 2u) & 0xfffu;
  recipe.nextPitch = advancePitch(core->mem_r32(kSpinPitch));
  const int32_t cameraGroup = (int32_t)core->mem_r32(kCameraOcclusionGroup);
  const int32_t groupCount = (int32_t)core->mem_r32(kEnvironmentOcclusionGroupCount);
  recipe.mainSelection = cameraGroup < groupCount ? cameraGroup : -1;
  recipe.portalCount = (int32_t)core->mem_r32(kPortalCount);
  if (recipe.portalCount <= 0) {
    recipe.status = Status::Ready;
    recipe.refusal = "none";
    return recipe;
  }
  if ((uint32_t)recipe.portalCount > kPortalCapacity) {
    return refuse(recipe, Status::InvalidPortalCount, "portal_count");
  }

  const world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram, sizeof(core->ram)));
  for (int32_t i = 0; i < recipe.portalCount; ++i) {
    const uint32_t portal = ram.r32(kPortals + (uint32_t)i * 4u);
    if (portal == 0u || (portal & 3u) != 0u || !ram.contains(portal, kPortalWorldSector + 4u)) {
      return refuse(recipe, Status::InvalidPortalPointer, "portal_pointer");
    }
    const int32_t sector = (int32_t)ram.r32(portal + kPortalWorldSector);
    if (sector < 0) {
      ++recipe.activePortals;
    } else {
      if ((uint32_t)sector >= 256u) {
        return refuse(recipe, Status::InvalidPortalSector, "portal_sector");
      }
      if (ram.r8(kBroadVisibility + (uint32_t)sector) == 0u) {
        continue;
      }
      ++recipe.activePortals;
    }
    const auto portalFrame = cyclorama_portal_mesh::prepareFrame(
        core, portal, (uint32_t)i, recipe.nextYaw, recipe.nextPitch);
    recipe.portalFrames.push_back(portalFrame);
    const Status portalStatus = classifyPortalFrame(portalFrame);
    if (portalStatus == Status::Ready) {
      ++recipe.validEmptyPortals;
      continue;
    }
    if (portalStatus == Status::ActivePortalDrawUnsupported) {
      return refuse(recipe, Status::ActivePortalDrawUnsupported, "active_portal_draw");
    }
    return refuse(recipe, Status::InvalidPortalRecipe, "portal_recipe");
  }
  recipe.status = Status::Ready;
  recipe.refusal = "none";
  return recipe;
}

void publishSpin(Core *core, const Recipe &recipe) {
  if (core == nullptr || recipe.status != Status::Ready) {
    return;
  }
  core->mem_w32(kSpinYaw, recipe.nextYaw);
  core->mem_w32(kSpinPitch, (uint32_t)recipe.nextPitch);
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::InvalidCore:
    return "invalid core";
  case Status::InvalidPortalCount:
    return "invalid portal count";
  case Status::InvalidPortalPointer:
    return "invalid portal pointer";
  case Status::InvalidPortalSector:
    return "invalid portal sector";
  case Status::InvalidPortalRecipe:
    return "invalid portal recipe";
  case Status::ActivePortalDrawUnsupported:
    return "active portal draw unsupported";
  }
  return "unknown";
}

} // namespace spyro::cyclorama_scene_recipe
