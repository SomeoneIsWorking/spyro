#include "core.h"
#include "game.h"
#include "moby_shadow_recipe.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::moby_shadow_recipe::Recipe;
using spyro::moby_shadow_recipe::Reject;
using spyro::moby_shadow_recipe::Status;

constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kShadowList = 0x800724f4u;
constexpr uint32_t kMobyShadows = 0x80075ef8u;
constexpr uint32_t kMoby = 0x80100000u;

// One Moby directly ahead of the camera with a flat shadow plane, no plane rotation and no depth
// offset. The camera is exactly orthogonal so the ground plane maps onto screen X/Y and the fan's
// four corners straddle the anchor. Its handedness is deliberate: 0x80059F8C rejects the whole
// shadow on NCLIP, so a camera that winds the fan away from the viewer produces no faces at all —
// which is the correct answer, not a fixture that happens to work.
std::unique_ptr<Game> shadowFixture(uint32_t shadowWord = 0x400u, int32_t radius = 256) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  const std::array<int16_t, 9> matrix{4096, 0, 0, 0, 0, -4096, 0, -4096, 0};
  for (size_t i = 0; i < matrix.size(); ++i) {
    core.mem_w16(kCamera + (uint32_t)i * 2u, (uint16_t)matrix[i]);
  }
  core.mem_w32(kMoby + 0x0cu, 0u);         // position X
  core.mem_w32(kMoby + 0x10u, 0u);         // position Y
  core.mem_w32(kMoby + 0x1cu, shadowWord); // shadow plane and its two rotation angles
  core.mem_w8(kMoby + 0x47u, 0u);          // m_DepthOffset
  core.mem_w32(kShadowList, kMoby);
  core.mem_w32(kShadowList + 4u, (uint32_t)radius);
  core.mem_w32(kMobyShadows + 8u, kShadowList + 8u);
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

void test_derive_projects_one_closed_four_point_fan() {
  const auto game = shadowFixture();
  const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.entries, 1u);
  CHECK_EQ(recipe.drawn, 1u);
  CHECK_EQ(recipe.faces.size(), 4u);
  float minX = 256, maxX = 256, minY = 120, maxY = 120;
  for (size_t i = 0; i < recipe.faces.size(); ++i) {
    const auto &face = recipe.faces[i];
    CHECK_EQ(face.fanOrdinal, i);
    CHECK_EQ(face.moby, kMoby);
    CHECK(face.vertices[0].screenX == 256 && face.vertices[0].screenY == 120);
    // The fan closes: this triangle's far corner is the next triangle's near corner.
    const auto &nextStart = recipe.faces[(i + 1u) % recipe.faces.size()].vertices[1];
    CHECK(face.vertices[2].screenX == nextStart.screenX);
    CHECK(face.vertices[2].screenY == nextStart.screenY);
    minX = std::min(minX, face.vertices[1].screenX);
    maxX = std::max(maxX, face.vertices[1].screenX);
    minY = std::min(minY, face.vertices[1].screenY);
    maxY = std::max(maxY, face.vertices[1].screenY);
  }
  CHECK(minX < 256 && maxX > 256 && minY < 120 && maxY > 120);
}

// The discriminator the census exists for: a rejected shadow must be counted by REASON, not
// silently produce the same empty recipe an empty list produces.
// The reject the fixture's handedness exists to avoid, asserted directly rather than assumed.
void test_reversed_winding_rejects_the_whole_shadow() {
  const auto game = shadowFixture();
  game->core.mem_w16(kCamera, (uint16_t)-4096); // mirror screen X, reversing the fan's winding
  const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.rejects[(size_t)Reject::Backfacing], 1u);
}

void test_shadowless_moby_is_counted_not_dropped() {
  const auto game = shadowFixture(0u);
  const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.entries, 1u);
  CHECK_EQ(recipe.drawn, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::NoShadowPlane], 1u);
}

void test_empty_list_is_valid_and_distinguishable() {
  const auto game = shadowFixture();
  game->core.mem_w32(kMobyShadows + 8u, kShadowList);
  const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.entries, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::NoShadowPlane], 0u);
}

void test_far_moby_is_rejected_at_the_view_limit() {
  const auto game = shadowFixture(0x1000u);
  const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.rejects[(size_t)Reject::BehindCamera], 1u);
}

void test_misaligned_or_reversed_cursor_refuses() {
  for (const uint32_t cursor : {kShadowList + 4u, kShadowList - 8u}) {
    const auto game = shadowFixture();
    game->core.mem_w32(kMobyShadows + 8u, cursor);
    const auto recipe = spyro::moby_shadow_recipe::derive(&game->core);
    CHECK(recipe.status == Status::InvalidState);
  }
}

// 0x80059F8C shifts by seven where the sixteen-point Spyro shadow shifts by nine. Reusing the
// sibling's constant would sort every Moby shadow four bins too near.
void test_retained_ot_formula_shift() {
  CHECK_EQ(spyro::moby_shadow_recipe::otBin(0x0800, 0x0800, 0x0400, 0, 7), 56);
  CHECK_EQ(spyro::moby_shadow_recipe::otBin(0x0800, 0x0800, 0x0400, 3, 7), 53);
  CHECK_EQ(spyro::moby_shadow_recipe::otBin(0x0800, 0x0800, 0x0400, 0, 9), 14);
}

void test_distance_fade_ramps_to_zero_at_the_far_limit() {
  CHECK_EQ(spyro::moby_shadow_recipe::distanceGrey(0), 0x80);
  CHECK_EQ(spyro::moby_shadow_recipe::distanceGrey(0xbff), 0x80);
  CHECK_EQ(spyro::moby_shadow_recipe::distanceGrey(0xc00), 0x80);
  CHECK_EQ(spyro::moby_shadow_recipe::distanceGrey(0xe00), 0x40);
  CHECK_EQ(spyro::moby_shadow_recipe::distanceGrey(0xfff), 0x00);
}

void test_missing_game_is_refused() {
  const auto core = std::make_unique<Core>();
  const auto recipe = spyro::moby_shadow_recipe::derive(core.get());
  CHECK(recipe.status == Status::InvalidCore);
}

} // namespace

int main() {
  RUN(derive_projects_one_closed_four_point_fan);
  RUN(reversed_winding_rejects_the_whole_shadow);
  RUN(shadowless_moby_is_counted_not_dropped);
  RUN(empty_list_is_valid_and_distinguishable);
  RUN(far_moby_is_rejected_at_the_view_limit);
  RUN(misaligned_or_reversed_cursor_refuses);
  RUN(retained_ot_formula_shift);
  RUN(distance_fade_ramps_to_zero_at_the_far_limit);
  RUN(missing_game_is_refused);
  return pt_summary();
}
