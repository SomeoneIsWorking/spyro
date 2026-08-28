#include "core.h"
#include "cyclorama_scene_recipe.h"
#include "testutil.h"

#include <cstdlib>
#include <fstream>
#include <memory>

namespace {

using spyro::cyclorama_scene_recipe::Recipe;
using spyro::cyclorama_scene_recipe::Status;

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();

  Harness() {
    core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
    core->rsub.projParams.setGeomScreen(341.0f);
    core->mem_w32(spyro::cyclorama_scene_recipe::kSpinYaw, 0xfffu);
    core->mem_w32(spyro::cyclorama_scene_recipe::kSpinPitch, 0x800u);
    core->mem_w32(spyro::cyclorama_scene_recipe::kCameraOcclusionGroup, 3u);
    core->mem_w32(spyro::cyclorama_scene_recipe::kEnvironmentOcclusionGroupCount, 4u);
  }
};

void setPortal(Harness &h, uint32_t index, uint32_t address, int32_t sector) {
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortals + index * 4u, address);
  h.core->mem_w32(address + 0x14u, (uint32_t)sector);
}

void test_no_portals_is_ready_and_pure() {
  Harness h;
  const Recipe recipe = spyro::cyclorama_scene_recipe::prepare(h.core.get());
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.mainSelection, 3);
  CHECK_EQ(recipe.nextYaw, 1u);
  CHECK_EQ(recipe.nextPitch, -2046);
  CHECK_EQ(recipe.portalCount, 0);
  CHECK_EQ(recipe.activePortals, 0u);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinYaw), 0xfffu);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinPitch), 0x800u);
}

void test_publish_spin_requires_ready_recipe() {
  Harness h;
  Recipe refused{};
  spyro::cyclorama_scene_recipe::publishSpin(h.core.get(), refused);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinYaw), 0xfffu);
  const Recipe ready = spyro::cyclorama_scene_recipe::prepare(h.core.get());
  spyro::cyclorama_scene_recipe::publishSpin(h.core.get(), ready);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinYaw), 1u);
  CHECK_EQ((int32_t)h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinPitch), -2046);
}

void test_inactive_portal_keeps_main_sky_ready() {
  Harness h;
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortalCount, 1u);
  setPortal(h, 0u, 0x80010000u, 7);
  const Recipe recipe = spyro::cyclorama_scene_recipe::prepare(h.core.get());
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.portalCount, 1);
  CHECK_EQ(recipe.activePortals, 0u);
}

void test_active_malformed_portal_refuses() {
  Harness h;
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortalCount, 2u);
  setPortal(h, 0u, 0x80010000u, 7);
  setPortal(h, 1u, 0x80010020u, -1);
  h.core->mem_w8(spyro::cyclorama_scene_recipe::kBroadVisibility + 7u, 0xffu);
  const Recipe recipe = spyro::cyclorama_scene_recipe::prepare(h.core.get());
  CHECK(recipe.status == Status::InvalidPortalRecipe);
  CHECK_EQ(recipe.activePortals, 1u);
}

void test_invalid_portal_shape_refuses() {
  Harness h;
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortalCount, 7u);
  CHECK(spyro::cyclorama_scene_recipe::prepare(h.core.get()).status == Status::InvalidPortalCount);
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortalCount, 1u);
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortals, 0u);
  CHECK(spyro::cyclorama_scene_recipe::prepare(h.core.get()).status ==
        Status::InvalidPortalPointer);
  h.core->mem_w32(spyro::cyclorama_scene_recipe::kPortals, 0x90000000u);
  CHECK(spyro::cyclorama_scene_recipe::prepare(h.core.get()).status ==
        Status::InvalidPortalPointer);
  setPortal(h, 0u, 0x80010000u, 256);
  CHECK(spyro::cyclorama_scene_recipe::prepare(h.core.get()).status == Status::InvalidPortalSector);
}

void test_portal_frame_classification_preserves_mask_ownership() {
  spyro::cyclorama_portal_mesh::PortalFrame offscreen{};
  offscreen.status = spyro::cyclorama_portal_mesh::Status::ValidEmpty;
  offscreen.maskVisible = false;
  CHECK(spyro::cyclorama_scene_recipe::classifyPortalFrame(offscreen) == Status::Ready);

  // A far portal has no 0x80050240 mesh, but an on-screen aperture still calls
  // the separate 0x8004FEA0 mask family and must refuse the incomplete owner.
  spyro::cyclorama_portal_mesh::PortalFrame visibleFar = offscreen;
  visibleFar.maskVisible = true;
  CHECK(spyro::cyclorama_scene_recipe::classifyPortalFrame(visibleFar) ==
        Status::ActivePortalDrawUnsupported);

  spyro::cyclorama_portal_mesh::PortalFrame visibleMid{};
  visibleMid.status = spyro::cyclorama_portal_mesh::Status::Ready;
  visibleMid.maskVisible = true;
  CHECK(spyro::cyclorama_scene_recipe::classifyPortalFrame(visibleMid) ==
        Status::ActivePortalDrawUnsupported);
}

void inspectSnapshotIfRequested() {
  const char *path = std::getenv("PSXPORT_FIELD_CYCLORAMA_SNAPSHOT");
  if (path == nullptr || path[0] == '\0') {
    return;
  }
  Harness h;
  std::ifstream input(path, std::ios::binary);
  CHECK(input.good());
  input.read(reinterpret_cast<char *>(h.core->ram), sizeof(h.core->ram));
  CHECK(input.gcount() == static_cast<std::streamsize>(sizeof(h.core->ram)));
  const uint32_t yaw = h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinYaw);
  const uint32_t pitch = h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinPitch);
  const Recipe recipe = spyro::cyclorama_scene_recipe::prepare(h.core.get());
  std::printf("field cyclorama snapshot: status=%s selection=%d portals=%d active=%u "
              "yaw=0x%X->0x%X pitch=%d->%d reason=%s\n",
              spyro::cyclorama_scene_recipe::statusName(recipe.status),
              recipe.mainSelection,
              recipe.portalCount,
              recipe.activePortals,
              yaw,
              recipe.nextYaw,
              (int32_t)pitch,
              recipe.nextPitch,
              recipe.refusal);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.mainSelection, 17);
  CHECK_EQ(recipe.portalCount, 5);
  CHECK_EQ(recipe.activePortals, 5u);
  CHECK_EQ(recipe.validEmptyPortals, 5u);
  CHECK_EQ(recipe.portalFrames.size(), 5u);
  CHECK_EQ(recipe.nextYaw, 0x3e4u);
  CHECK_EQ(recipe.nextPitch, 0x80);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinYaw), yaw);
  CHECK_EQ(h.core->mem_r32(spyro::cyclorama_scene_recipe::kSpinPitch), pitch);
}

} // namespace

int main() {
  RUN(no_portals_is_ready_and_pure);
  RUN(publish_spin_requires_ready_recipe);
  RUN(inactive_portal_keeps_main_sky_ready);
  RUN(active_malformed_portal_refuses);
  RUN(invalid_portal_shape_refuses);
  RUN(portal_frame_classification_preserves_mask_ownership);
  inspectSnapshotIfRequested();
  return pt_summary();
}
