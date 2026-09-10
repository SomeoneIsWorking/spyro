#include "core.h"
#include "cyclorama_portal_mesh_recipe.h"
#include "testutil.h"

#include <cstdlib>
#include <fstream>
#include <memory>

namespace {

using spyro::cyclorama_portal_mesh::PortalFrame;
using spyro::cyclorama_portal_mesh::Recipe;
using spyro::cyclorama_portal_mesh::Status;

struct Harness {
  std::unique_ptr<Core> core = std::make_unique<Core>();

  Harness() {
    core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
    core->rsub.projParams.setGeomScreen(341.0f);
  }
};

void test_frame_refuses_unset_projection_and_bad_shape() {
  auto core = std::make_unique<Core>();
  CHECK(spyro::cyclorama_portal_mesh::prepareFrame(core.get(), 0x80010000u, 0u, 0u, 0).status ==
        Status::ProjectionUnset);
  core->rsub.projParams.setGeomOffset(256.0f, 120.0f);
  core->rsub.projParams.setGeomScreen(341.0f);
  CHECK(spyro::cyclorama_portal_mesh::prepareFrame(core.get(), 0x90000000u, 0u, 0u, 0u).status ==
        Status::InvalidPortal);
  core->mem_w32(0x80010000u + 4u, 2u);
  CHECK(spyro::cyclorama_portal_mesh::prepareFrame(core.get(), 0x80010000u, 0u, 0u, 0).status ==
        Status::InvalidPointCount);
}

void test_build_refusal_is_atomic() {
  Harness h;
  PortalFrame frame{};
  frame.status = Status::Ready;
  frame.portal = 0x80010000u;
  frame.asset = 0x80011000u;
  frame.edges = {{0, 0, 512, 0}, {512, 0, 512, 240}, {512, 240, 0, 240}, {0, 240, 0, 0}};
  h.core->mem_w32(frame.asset, 257u);
  const Recipe recipe = spyro::cyclorama_portal_mesh::build(h.core.get(), frame);
  CHECK(recipe.status == Status::InvalidAsset);
  CHECK(recipe.faces.empty());
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
  const uint32_t nextYaw = (h.core->mem_r32(0x80075858u) + 2u) & 0xfffu;
  int32_t nextPitch = (int32_t)((h.core->mem_r32(0x800758fcu) + 2u) & 0xfffu);
  if (nextPitch >= 0x801) {
    nextPitch -= 0x1000;
  }
  if (nextPitch >= 0x81) {
    nextPitch = 0x80;
  }
  PortalFrame frame{};
  PortalFrame nearFrame{};
  for (uint32_t i = 0; i < 5u; ++i) {
    const uint32_t portal = h.core->mem_r32(0x80078640u + i * 4u);
    const PortalFrame observed =
        spyro::cyclorama_portal_mesh::prepareFrame(h.core.get(), portal, i, nextYaw, nextPitch);
    std::printf("portal%u frame: status=%s portal=0x%08X asset=0x%08X distance=%u shift=%u "
                "points=%u edges=%zu mask=%d box=%d,%d..%d,%d ot=%u fade=%u tint=0x%08X "
                "reason=%s\n",
                i,
                spyro::cyclorama_portal_mesh::statusName(observed.status),
                observed.portal,
                observed.asset,
                observed.distance,
                observed.distanceShift,
                observed.pointCount,
                observed.edges.size(),
                observed.maskVisible ? 1 : 0,
                observed.clipLeft,
                observed.clipTop,
                observed.clipRight,
                observed.clipBottom,
                observed.otBin,
                observed.fadeFactor,
                observed.tintColor,
                observed.refusal);
    if (i == 2u) {
      frame = observed;
    }
    if (i == 0u) {
      nearFrame = observed;
    }
  }
  CHECK(frame.status == Status::ValidEmpty);
  CHECK_EQ(frame.portal, 0x800fb980u);
  CHECK_EQ(frame.asset, 0x800fb9dcu);
  CHECK_EQ(frame.pointCount, 5u);
  CHECK(!frame.maskVisible);

  const Recipe nearRecipe = spyro::cyclorama_portal_mesh::build(h.core.get(), nearFrame);
  std::printf("portal0 near mesh: status=%s objects=%u survivors=%u vertices=%u authored=%u "
              "candidates=%u box_reject=%u aperture_reject=%u accepted=%u triangles=%u reason=%s\n",
              spyro::cyclorama_portal_mesh::statusName(nearRecipe.status),
              nearRecipe.assetObjects,
              nearRecipe.survivingObjects,
              nearRecipe.projectedVertices,
              nearRecipe.authoredCandidates,
              nearRecipe.candidates,
              nearRecipe.boxRejected,
              nearRecipe.apertureRejected,
              nearRecipe.sourceAccepted,
              nearRecipe.emittedTriangles,
              nearRecipe.refusal);
  CHECK(nearFrame.status == spyro::cyclorama_portal_mesh::Status::NearFamilyUnsupported);
  CHECK(nearRecipe.status == Status::Ready);
  CHECK(!nearRecipe.faces.empty());

  // This frame does not actually invoke 0x80050240. Keep the asset decoder
  // independently observable with a positive aperture; do not relabel the
  // retained frame as a portal draw.
  frame.status = Status::Ready;
  frame.refusal = "none";
  frame.maskVisible = true;
  frame.edges = {{0, 0, 512, 0}, {512, 0, 512, 240}, {512, 240, 0, 240}, {0, 240, 0, 0}};
  frame.clipLeft = 0;
  frame.clipTop = 0;
  frame.clipRight = 512;
  frame.clipBottom = 240;
  frame.fadeFactor = frame.distance - 0x3000u;
  frame.tintColor = 0x00808080u;
  const Recipe recipe = spyro::cyclorama_portal_mesh::build(h.core.get(), frame);
  std::printf("portal2 mesh: status=%s objects=%u survivors=%u vertices=%u authored=%u "
              "candidates=%u box_reject=%u aperture_reject=%u accepted=%u triangles=%u reason=%s\n",
              spyro::cyclorama_portal_mesh::statusName(recipe.status),
              recipe.assetObjects,
              recipe.survivingObjects,
              recipe.projectedVertices,
              recipe.authoredCandidates,
              recipe.candidates,
              recipe.boxRejected,
              recipe.apertureRejected,
              recipe.sourceAccepted,
              recipe.emittedTriangles,
              recipe.refusal);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.assetObjects, 35u);
  CHECK_EQ(recipe.authoredCandidates, 2897u);
  CHECK(!recipe.faces.empty());
}

// A portal the player has walked into projects every aperture point off screen with a negative view
// Z. Retail draws nothing for it; the port used to build an aperture out of those saturated points
// and hand it to the mesh recipe, which then clipped every candidate away and aborted the frame.
void test_a_portal_behind_the_camera_is_hidden() {
  using spyro::cyclorama_portal_mesh::MeshVisibility;
  PortalFrame frame{};
  frame.edges.push_back({0, 0, 10, 10});
  // The measured points from the aborting Artisans frame, saturated at the GTE's screen clamp.
  frame.points = {{-1022, 1021, -4508},
                  {-1022, -1022, -5124},
                  {-908, -1022, -5055},
                  {623, -1022, -4824},
                  {913, 1021, -4134}};
  CHECK(spyro::cyclorama_portal_mesh::meshVisibility(frame, 684, -23645, false) ==
        MeshVisibility::Hidden);
  // The same aperture in FRONT of the camera qualifies: it encloses every screen edge, which is how
  // a portal the camera is inside still draws even though no edge crosses the screen.
  for (auto &point : frame.points) {
    point[2] = -point[2];
  }
  CHECK(spyro::cyclorama_portal_mesh::meshVisibility(frame, 684, 23645, false) ==
        MeshVisibility::QualifiedNoPointOnScreen);
  CHECK(spyro::cyclorama_portal_mesh::meshVisibility(frame, 684, 23645, true) ==
        MeshVisibility::Visible);
}

// An ordinary mid-distance portal: a small aperture on screen, in front, with edges.
void test_a_portal_on_screen_is_visible() {
  using spyro::cyclorama_portal_mesh::MeshVisibility;
  PortalFrame frame{};
  frame.edges.push_back({100, 100, 200, 100});
  frame.points = {{100, 100, 4000}, {200, 100, 4000}, {150, 180, 4000}};
  CHECK(spyro::cyclorama_portal_mesh::meshVisibility(frame, 684, 12000, true) ==
        MeshVisibility::Visible);
  // With no edge retained it falls through to the whole-screen test, which this small aperture
  // fails, so it is hidden rather than silently drawn against an empty half-plane list.
  frame.edges.clear();
  CHECK(spyro::cyclorama_portal_mesh::meshVisibility(frame, 684, 12000, true) ==
        MeshVisibility::Hidden);
}

} // namespace

int main() {
  RUN(frame_refuses_unset_projection_and_bad_shape);
  RUN(build_refusal_is_atomic);
  RUN(a_portal_behind_the_camera_is_hidden);
  RUN(a_portal_on_screen_is_visible);
  inspectSnapshotIfRequested();
  return pt_summary();
}
