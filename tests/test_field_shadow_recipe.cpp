#include "core.h"
#include "field_shadow_recipe.h"
#include "game.h"
#include "testutil.h"

#include <algorithm>
#include <array>
#include <memory>

namespace {

using spyro::field_shadow_recipe::Recipe;
using spyro::field_shadow_recipe::Status;

std::unique_ptr<Game> shadowFixture() {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  constexpr uint32_t camera = 0x80076dd0u;
  constexpr uint32_t models = 0x80090000u;
  constexpr uint32_t animation = 0x80091000u;
  constexpr uint32_t radii = 0x00092000u;
  constexpr uint32_t shadow = 0x8007aa10u;
  // An exact orthogonal camera maps the ground X/Z plane onto screen X/Y. Its view-Z
  // points down the second input axis; the anchor below is therefore 1024 units ahead.
  const std::array<int16_t, 9> matrix{4096, 0, 0, 0, 0, 4096, 0, -4096, 0};
  for (size_t i = 0; i < matrix.size(); ++i) {
    core.mem_w16(camera + static_cast<uint32_t>(i) * 2u, static_cast<uint16_t>(matrix[i]));
  }
  core.mem_w32(shadow + 0x18u, 1024u);
  core.mem_w32(0x80076378u, models);
  core.mem_w32(models + 0x38u, animation);
  core.mem_w32(animation + 0x24u, radii >> 1u); // production frame reference encoding
  for (uint32_t i = 0; i < 8; ++i) {
    core.mem_w8(radii + i, 128);
  }
  // Synthetic square perimeter, ordered around the anchor. No retail shape bytes are needed.
  const std::array<std::array<int16_t, 2>, 16> directions{{{1024, 0},
                                                           {1024, 512},
                                                           {1024, 1024},
                                                           {512, 1024},
                                                           {0, 1024},
                                                           {-512, 1024},
                                                           {-1024, 1024},
                                                           {-1024, 512},
                                                           {-1024, 0},
                                                           {-1024, -512},
                                                           {-1024, -1024},
                                                           {-512, -1024},
                                                           {0, -1024},
                                                           {512, -1024},
                                                           {1024, -1024},
                                                           {1024, -512}}};
  for (size_t i = 0; i < directions.size(); ++i) {
    const uint32_t address = 0x8006e268u + static_cast<uint32_t>(i) * 4u;
    core.mem_w16(address, static_cast<uint16_t>(directions[i][1]));
    core.mem_w16(address + 2u, static_cast<uint16_t>(directions[i][0]));
  }
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(256);
  game->gte.REG[56] = 256u << 16u;
  game->gte.REG[57] = 120u << 16u;
  game->gte.REG[58] = 256;
  return game;
}

void checkSameGeometry(const Recipe &a, const Recipe &b) {
  CHECK(a.status == b.status);
  CHECK_EQ(a.faceCount, b.faceCount);
  for (size_t face = 0; face < a.faceCount; ++face) {
    CHECK_EQ(a.faces[face].otBin, b.faces[face].otBin);
    CHECK_EQ(a.faces[face].fanOrdinal, b.faces[face].fanOrdinal);
    for (size_t vertex = 0; vertex < 3; ++vertex) {
      const auto &first = a.faces[face].vertices[vertex];
      const auto &second = b.faces[face].vertices[vertex];
      CHECK(first.sx == second.sx && first.sy == second.sy);
      CHECK(first.screenX == second.screenX && first.screenY == second.screenY);
      CHECK(first.viewZ == second.viewZ && first.sz == second.sz);
    }
  }
}

void test_saturating_anchor_still_produces_the_retail_fan() {
  // func_80059A48 reads SXY2/SZ3/MAC1-3 and never tests FLAG, so an anchor behind the near plane —
  // which is what the frames right after a level entrance look like — is still a drawn shadow. The
  // recipe must report the saturation rather than refuse it.
  const auto game = shadowFixture();
  game->core.mem_w32(0x8007aa10u + 0x18u, (uint32_t)(int32_t)-1024);
  const auto recipe = spyro::field_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.faceCount, 16u);
  // Bit 31 is the GTE's error sum; bit 17 is the H/SZ3 divide overflow the clamped SZ3 forces.
  CHECK((recipe.anchorFlags & 0x80000000u) != 0u);
  CHECK((recipe.anchorFlags & (1u << 17u)) != 0u);
}

void test_derive_projects_visible_closed_fan() {
  const auto game = shadowFixture();
  const auto recipe = spyro::field_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.faceCount, 16u);
  float minX = 256, maxX = 256, minY = 120, maxY = 120;
  for (size_t i = 0; i < recipe.faceCount; ++i) {
    const auto &face = recipe.faces[i];
    CHECK_EQ(face.fanOrdinal, i);
    CHECK_EQ(face.otBin, 8);
    CHECK(face.vertices[0].screenX == 256 && face.vertices[0].screenY == 120);
    CHECK_EQ(face.vertices[0].sz, 1024);
    const auto &nextStart = recipe.faces[(i + 1u) % recipe.faceCount].vertices[1];
    CHECK(face.vertices[2].screenX == nextStart.screenX);
    CHECK(face.vertices[2].screenY == nextStart.screenY);
    minX = std::min(minX, face.vertices[1].screenX);
    maxX = std::max(maxX, face.vertices[1].screenX);
    minY = std::min(minY, face.vertices[1].screenY);
    maxY = std::max(maxY, face.vertices[1].screenY);
  }
  CHECK(minX < 256 && maxX > 256 && minY < 120 && maxY > 120);
}

void test_ambient_gte_projection_cannot_move_native_shadow() {
  const auto game = shadowFixture();
  const auto baseline = spyro::field_shadow_recipe::derive(&game->core);
  CHECK(baseline.status == Status::Ready);
  for (const uint32_t h : {341u, 0u}) {
    game->gte.REG[56] = 100u << 16u;
    game->gte.REG[57] = 42u << 16u;
    game->gte.REG[58] = h;
    const auto changed = spyro::field_shadow_recipe::derive(&game->core);
    CHECK(changed.status == Status::Ready);
    checkSameGeometry(baseline, changed);
    CHECK_EQ(game->gte.REG[56], 100u << 16u);
    CHECK_EQ(game->gte.REG[57], 42u << 16u);
    CHECK_EQ(game->gte.REG[58], h);
  }
}

void test_owned_projection_moves_shadow_and_invalid_projection_refuses() {
  const auto game = shadowFixture();
  game->core.rsub.projParams.setGeomOffset(342, 144);
  game->core.rsub.projParams.setGeomScreen(341);
  const auto changed = spyro::field_shadow_recipe::derive(&game->core);
  CHECK(changed.status == Status::Ready);
  CHECK_EQ(changed.faceCount, 16u);
  CHECK(changed.faces[0].vertices[0].screenX == 342);
  CHECK(changed.faces[0].vertices[0].screenY == 144);
  // The first perimeter point has positive view X: a longer focal length must widen it.
  CHECK(changed.faces[0].vertices[1].screenX - 342 > 32);
  game->core.rsub.projParams.setGeomScreen(0);
  CHECK(spyro::field_shadow_recipe::derive(&game->core).status == Status::UnpublishedProjection);
  game->core.rsub.projParams = ProjParams{};
  CHECK(spyro::field_shadow_recipe::derive(&game->core).status == Status::UnpublishedProjection);
}

void test_retained_ot_formula() {
  CHECK_EQ(spyro::field_shadow_recipe::otBin(0x0742, 0x0713, 0x065e, 3), 10);
  CHECK_EQ(spyro::field_shadow_recipe::otBin(0x0800, 0x0800, 0x0400, 3), 11);
}

void test_retained_radius_interpolation() {
  CHECK_EQ(spyro::field_shadow_recipe::interpolateRadius(10, 30, 4), 15);
  CHECK_EQ(spyro::field_shadow_recipe::interpolateRadius(10, 30, 16), 30);
}

void test_missing_game_is_refused() {
  const auto core = std::make_unique<Core>();
  const auto recipe = spyro::field_shadow_recipe::derive(core.get());
  CHECK(recipe.status == spyro::field_shadow_recipe::Status::InvalidCore);
}

} // namespace

int main() {
  RUN(derive_projects_visible_closed_fan);
  RUN(saturating_anchor_still_produces_the_retail_fan);
  RUN(ambient_gte_projection_cannot_move_native_shadow);
  RUN(owned_projection_moves_shadow_and_invalid_projection_refuses);
  RUN(retained_ot_formula);
  RUN(retained_radius_interpolation);
  RUN(missing_game_is_refused);
  return pt_summary();
}
