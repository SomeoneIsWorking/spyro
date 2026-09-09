// 0x800584C4 is the only render producer in this port that also writes guest state: it burns each
// sparkle's lifetime by the frame delta, spins its angle, and kills a sparkle whose projection it
// rejects. These cases pin both halves — the two crossed lines it draws, and the writes it hands
// back — because a derivation that only got the geometry right would leave sparkles alive forever.
#include "core.h"
#include "game.h"
#include "sparkle_recipe.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::sparkle_recipe::Reject;
using spyro::sparkle_recipe::Status;

constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kSparkles = 0x80077108u;
constexpr uint32_t kStride = 0x18u;
constexpr uint32_t kSine = 0x8006cbf8u;

// One sparkle directly ahead of the camera. The guest feeds the GTE (camY - y, camZ - z, x - camX),
// so it is the third lane that becomes the view depth and the camera sits back along X. Only record
// 0 carries a lifetime, so the other seven are counted as expired rather than silently skipped.
std::unique_ptr<Game> sparkleFixture(uint8_t life = 8,
                                     uint8_t total = 16,
                                     int32_t depth = 0x800,
                                     uint32_t sizeWord = 0xff20u) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  const std::array<int16_t, 9> matrix{4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
  for (size_t i = 0; i < matrix.size(); ++i) {
    core.mem_w16(kCamera + (uint32_t)i * 2u, (uint16_t)matrix[i]);
  }
  core.mem_w32(kCamera + 0x28u, (uint32_t)(-depth));
  core.mem_w32(kCamera + 0x2cu, 0u);
  core.mem_w32(kCamera + 0x30u, 0u);
  // A quarter-turn sine table: entry 0 is zero and the cosine a quarter later is one.
  for (uint32_t i = 0; i < 256u; ++i) {
    core.mem_w16(kSine + i * 2u, 0u);
  }
  core.mem_w16(kSine + 0x80u, (uint16_t)4096);

  for (uint32_t i = 0; i < 8u; ++i) {
    for (uint32_t w = 0; w < kStride; w += 4u) {
      core.mem_w32(kSparkles + i * kStride + w, 0u);
    }
  }
  core.mem_w32(kSparkles + 0u, 0u);
  core.mem_w32(kSparkles + 4u, 0u);
  core.mem_w32(kSparkles + 8u, 0u);
  // life, total lifetime, angle, spin.
  core.mem_w32(kSparkles + 0x0cu,
               (uint32_t)life | ((uint32_t)total << 8) | (2u << 16) | (1u << 24));
  core.mem_w32(kSparkles + 0x10u, 0x00204060u);
  core.mem_w32(kSparkles + 0x14u, sizeWord);
  core.rsub.projParams.setGeomOffset(160, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

void test_missing_game_is_refused() {
  CHECK(spyro::sparkle_recipe::derive(nullptr, 1).status == Status::InvalidCore);
}

void test_live_sparkle_emits_two_crossed_lines() {
  const auto game = sparkleFixture();
  const auto recipe = spyro::sparkle_recipe::derive(&game->core, 1);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.alive, 1u);
  CHECK_EQ(recipe.drawn, 1u);
  // Two GP0 lines, not one primitive with four corners: the cross has no shared vertex.
  CHECK_EQ(recipe.lines.size(), (size_t)2);
  CHECK_EQ(recipe.lines[0].chainOrdinal, 0u);
  CHECK_EQ(recipe.lines[1].chainOrdinal, 1u);
  CHECK_EQ(recipe.lines[0].colour, 0x204060u);
  CHECK_EQ(recipe.lines[0].otBin, recipe.lines[1].otBin);
  // The seven records with no lifetime left are reported, not absent.
  CHECK_EQ(recipe.rejects[(size_t)Reject::Expired], 7u);
}

void test_lifetime_is_burned_and_the_angle_spun() {
  const auto game = sparkleFixture(8, 16);
  const auto recipe = spyro::sparkle_recipe::derive(&game->core, 3);
  CHECK_EQ(recipe.writes.size(), (size_t)8);
  const auto &write = recipe.writes[0];
  CHECK_EQ(write.record, kSparkles);
  CHECK_EQ((int)write.lifetime, 5);
  CHECK(write.angleWritten);
  // angle 2 plus a spin of 1 per tick over three ticks.
  CHECK_EQ((int)write.angle, 5);
}

void test_an_expired_sparkle_is_killed_without_an_angle_write() {
  const auto game = sparkleFixture(2, 16);
  const auto recipe = spyro::sparkle_recipe::derive(&game->core, 4);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.alive, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::Expired], 8u);
  // Retail zeroes the life byte before it has computed a new angle, so the angle keeps its value.
  CHECK(!recipe.writes[0].angleWritten);
  CHECK_EQ((int)recipe.writes[0].lifetime, 0);
}

void test_a_culled_sparkle_is_killed_but_keeps_its_angle() {
  // The record's far limit is byte 1 of the size word; 0x0100 sits in front of the fixture's depth.
  const auto game = sparkleFixture(8, 16, 0x800, 0x0120u);
  const auto recipe = spyro::sparkle_recipe::derive(&game->core, 1);
  CHECK_EQ(recipe.rejects[(size_t)Reject::TooFar], 1u);
  CHECK_EQ(recipe.lines.size(), (size_t)0);
  // The distinguishing part: the cull still writes back, and it writes the SPUN angle with a dead
  // lifetime, because retail stores the angle before it can decide to cull.
  CHECK(recipe.writes[0].angleWritten);
  CHECK_EQ((int)recipe.writes[0].angle, 3);
  CHECK_EQ((int)recipe.writes[0].lifetime, 0);
}

void test_commit_applies_the_writes_to_guest_memory() {
  const auto game = sparkleFixture(8, 16);
  const auto recipe = spyro::sparkle_recipe::derive(&game->core, 3);
  spyro::sparkle_recipe::commit(&game->core, recipe);
  CHECK_EQ((int)game->core.mem_r8(kSparkles + 0x0cu), 5);
  CHECK_EQ((int)game->core.mem_r8(kSparkles + 0x0eu), 5);
  // A record that was already dead stays dead rather than wrapping to 0xff.
  CHECK_EQ((int)game->core.mem_r8(kSparkles + kStride + 0x0cu), 0);
}

void test_ot_bin_pulls_toward_the_viewer_and_steps_past_the_split() {
  CHECK_EQ(spyro::sparkle_recipe::otBin(0x100), 2);
  // Anything within six bins of the viewer floors at zero rather than wrapping behind it.
  CHECK_EQ(spyro::sparkle_recipe::otBin(0x80), 0);
  CHECK_EQ(spyro::sparkle_recipe::otBin(0x40), 0);
  // 0x2100 >> 5 is 0x108, so the pulled bin lands past the split and takes the far step.
  CHECK_EQ(spyro::sparkle_recipe::otBin(0x2100), 0x102 + 0x46);
  // A bin at the split itself does not: the step is strictly past it.
  CHECK_EQ(spyro::sparkle_recipe::otBin(0x20c0), 0x100);
}

void test_on_screen_window_rejects_each_edge() {
  CHECK(spyro::sparkle_recipe::onScreen((100u << 16) | 100u));
  CHECK(!spyro::sparkle_recipe::onScreen(0u));
  CHECK(!spyro::sparkle_recipe::onScreen(0x00000064u)); // y at the top edge
  CHECK(!spyro::sparkle_recipe::onScreen(0x01000064u)); // y past the bottom edge
  CHECK(!spyro::sparkle_recipe::onScreen(0x00500000u)); // x at the left edge
  CHECK(!spyro::sparkle_recipe::onScreen(0x00500200u)); // x past the right edge
}

} // namespace

int main() {
  RUN(missing_game_is_refused);
  RUN(live_sparkle_emits_two_crossed_lines);
  RUN(lifetime_is_burned_and_the_angle_spun);
  RUN(an_expired_sparkle_is_killed_without_an_angle_write);
  RUN(a_culled_sparkle_is_killed_but_keeps_its_angle);
  RUN(commit_applies_the_writes_to_guest_memory);
  RUN(ot_bin_pulls_toward_the_viewer_and_steps_past_the_split);
  RUN(on_screen_window_rejects_each_edge);
  return pt_summary();
}
