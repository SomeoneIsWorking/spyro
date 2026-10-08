#include "core.h"
#include "cyclorama_mask_recipe.h"
#include "testutil.h"

#include <cstdint>
#include <memory>

namespace {

void test_source_triangles_are_clipped_and_flat_colored() {
  auto core = std::make_unique<Core>();
  constexpr uint32_t asset = 0x80010000u;
  core->mem_w32(asset + 0x10u, 0x00112233u);
  spyro::cyclorama_portal_mesh::PortalFrame frame{};
  frame.status = spyro::cyclorama_portal_mesh::Status::NearFamilyUnsupported;
  frame.asset = asset;
  frame.portalOrdinal = 2;
  frame.otBin = 77;
  frame.maskVisible = true;
  frame.edges = {
      {100, 50, 400, 50}, {400, 50, 400, 200}, {400, 200, 100, 200}, {100, 200, 100, 50}};

  const auto recipe = spyro::cyclorama_mask_recipe::build(core.get(), frame);
  CHECK(recipe.status == spyro::cyclorama_mask_recipe::Status::Ready);
  CHECK_EQ(recipe.portalOrdinal, 2u);
  CHECK_EQ(recipe.otBin, 77u);
  CHECK(!recipe.faces.empty());
  for (const auto &face : recipe.faces) {
    CHECK_EQ(face.rgb, 0x00112233u);
    for (const auto &vertex : face.vertices) {
      CHECK(vertex.sx >= 100 && vertex.sx <= 400);
      CHECK(vertex.sy >= 50 && vertex.sy <= 200);
    }
  }
}

void test_empty_aperture_is_valid_empty() {
  auto core = std::make_unique<Core>();
  spyro::cyclorama_portal_mesh::PortalFrame frame{};
  frame.status = spyro::cyclorama_portal_mesh::Status::ValidEmpty;
  const auto recipe = spyro::cyclorama_mask_recipe::build(core.get(), frame);
  CHECK(recipe.status == spyro::cyclorama_mask_recipe::Status::ValidEmpty);
  CHECK(recipe.faces.empty());
}

} // namespace

int main() {
  RUN(source_triangles_are_clipped_and_flat_colored);
  RUN(empty_aperture_is_valid_empty);
  return pt_summary();
}
