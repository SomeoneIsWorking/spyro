#include "core.h"
#include "game.h"
#include "guest_globals.h"
#include "moby_shadow_recipe.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::moby_shadow_recipe::Recipe;
using spyro::moby_shadow_recipe::Reject;
using spyro::moby_shadow_recipe::Status;

using spyro::guest::kCamera;
constexpr uint32_t kMoby = 0x80100000u;

// One Moby directly ahead of the camera with a flat shadow plane, no plane rotation and no depth
// offset. The camera is exactly orthogonal so the ground plane maps onto screen X/Y and the fan's
// four corners straddle the anchor. Its handedness is deliberate: 0x80059F8C rejects the whole
// shadow on NCLIP, so a camera that winds the fan away from the viewer produces no faces at all —
// which is the correct answer, not a fixture that happens to work.
std::unique_ptr<Game> shadowFixture(uint32_t shadowWord = 0x400u) {
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
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

// The recipe reads the DRAWN shadow list the three Moby passes publish, not the guest's list at
// 0x800724F4: at 16:9 the drawn list also carries margin Mobys the guest never stages.
spyro::moby_shadow_list::List oneShadow(int32_t radius = 256) {
  return {{.moby = kMoby, .radius = (uint32_t)radius}};
}

psxport::native_projection::ProjectionParams projectionAbout(int32_t ofx) {
  psxport::native_projection::ProjectionParams out{};
  out.ofx = ofx << 16;
  out.ofy = 120 << 16;
  out.h = 256;
  return out;
}

Recipe deriveNative(Core &core, const spyro::moby_shadow_list::List &entries = oneShadow()) {
  return spyro::moby_shadow_recipe::derive(&core, entries, projectionAbout(256), 512);
}

void test_derive_projects_one_closed_four_point_fan() {
  const auto game = shadowFixture();
  const auto recipe = deriveNative(game->core);
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
  const auto recipe = deriveNative(game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.rejects[(size_t)Reject::Backfacing], 1u);
}

void test_shadowless_moby_is_counted_not_dropped() {
  const auto game = shadowFixture(0u);
  const auto recipe = deriveNative(game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.entries, 1u);
  CHECK_EQ(recipe.drawn, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::NoShadowPlane], 1u);
}

void test_empty_list_is_valid_and_distinguishable() {
  const auto game = shadowFixture();
  const auto recipe = deriveNative(game->core, {});
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.entries, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::NoShadowPlane], 0u);
}

void test_far_moby_is_rejected_at_the_view_limit() {
  const auto game = shadowFixture(0x1000u);
  const auto recipe = deriveNative(game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.rejects[(size_t)Reject::BehindCamera], 1u);
}

void test_an_oversized_list_refuses() {
  const auto game = shadowFixture();
  const spyro::moby_shadow_list::List entries(4097u, oneShadow().front());
  CHECK(deriveNative(game->core, entries).status == Status::InvalidState);
}

// Retail keeps a shadow whose anchor is -8 < sx < 0x208: eight pixels of slack either side of its
// 512-px screen. The fan is drawn about the frame's own centre with the slack on the frame's own
// edges; it used to be projected about the 4:3 centre and clipped at 512 at every aspect, so a
// margin Moby's shadow was either missing or drawn 86 px left of its Moby.
void test_the_anchor_window_follows_the_drawn_frame_at_both_edges() {
  const auto game = shadowFixture();
  const auto at = [&](int32_t anchorX, int32_t clipRight) {
    return spyro::moby_shadow_recipe::derive(
        &game->core, oneShadow(), projectionAbout(anchorX), clipRight);
  };
  const auto anchored = at(342, 684);
  CHECK(anchored.status == Status::Ready);
  CHECK(anchored.faces[0].vertices[0].screenX == 342);
  // Left edge, identical at both widths.
  for (const int32_t right : {512, 684}) {
    CHECK_EQ(at(-8, right).rejects[(size_t)Reject::OffScreen], 1u);
    CHECK_EQ(at(-7, right).drawn, 1u);
  }
  // Right edge: 0x208 at 4:3, 684 + 8 at 16:9.
  CHECK_EQ(at(519, 512).drawn, 1u);
  CHECK_EQ(at(520, 512).rejects[(size_t)Reject::OffScreen], 1u);
  CHECK_EQ(at(600, 684).drawn, 1u);
  CHECK_EQ(at(691, 684).drawn, 1u);
  CHECK_EQ(at(692, 684).rejects[(size_t)Reject::OffScreen], 1u);
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
  const auto recipe = spyro::moby_shadow_recipe::derive(core.get(), {}, projectionAbout(256), 512);
  CHECK(recipe.status == Status::InvalidCore);
}

} // namespace

int main() {
  RUN(derive_projects_one_closed_four_point_fan);
  RUN(reversed_winding_rejects_the_whole_shadow);
  RUN(shadowless_moby_is_counted_not_dropped);
  RUN(empty_list_is_valid_and_distinguishable);
  RUN(far_moby_is_rejected_at_the_view_limit);
  RUN(an_oversized_list_refuses);
  RUN(the_anchor_window_follows_the_drawn_frame_at_both_edges);
  RUN(retained_ot_formula_shift);
  RUN(distance_fade_ramps_to_zero_at_the_far_limit);
  RUN(missing_game_is_refused);
  return pt_summary();
}
