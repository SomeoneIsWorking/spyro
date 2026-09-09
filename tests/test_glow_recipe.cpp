// 0x800580F4 fans semi-transparent Gouraud triangles from one projected centre out to a ring of
// black points, sixteen records at a time. Nothing in the port called it at all, so these cases pin
// the shape of what it produces and the cases where producing nothing is the right answer.
#include "core.h"
#include "game.h"
#include "glow_recipe.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::glow_recipe::Recipe;
using spyro::glow_recipe::Reject;
using spyro::glow_recipe::Status;

constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kGlows = 0x80078800u;
constexpr uint32_t kRing = 0x80100000u;
constexpr uint32_t kPosition = 0x80100100u;

// One record directly ahead of the camera, with a three-point ring. The camera matrix is the
// identity and the camera sits back along X, because the guest feeds the GTE (camY - y, camZ - z,
// x - camX) and it is that third lane that becomes the view depth. Only record 0 carries a point
// count, so the other fifteen are counted as empty rather than silently skipped.
std::unique_ptr<Game> glowFixture(uint32_t points = 3, int32_t depth = 0x800, int32_t bias = 0) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  const std::array<int16_t, 9> matrix{4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
  for (size_t i = 0; i < matrix.size(); ++i) {
    core.mem_w16(kCamera + (uint32_t)i * 2u, (uint16_t)matrix[i]);
  }
  core.mem_w32(kCamera + 0x28u, (uint32_t)(-depth));
  core.mem_w32(kCamera + 0x2Cu, 0u);
  core.mem_w32(kCamera + 0x30u, 0u);

  core.mem_w32(kPosition, 0u);
  core.mem_w32(kPosition + 4u, 0u);
  core.mem_w32(kPosition + 8u, 0u);
  // A ring wide enough that the scaled offsets land several pixels off the centre.
  const std::array<std::array<int32_t, 2>, 3> ring{
      std::array<int32_t, 2>{0x100, 0}, {0, 0x100}, {-0x100, 0}};
  for (uint32_t i = 0; i < points && i < ring.size(); ++i) {
    core.mem_w32(kRing + i * 8u, (uint32_t)ring[i][0]);
    core.mem_w32(kRing + i * 8u + 4u, (uint32_t)ring[i][1]);
  }

  core.mem_w32(kGlows + 0x00u, points);
  core.mem_w32(kGlows + 0x04u, kRing);
  core.mem_w32(kGlows + 0x08u, kPosition);
  core.mem_w32(kGlows + 0x0Cu, 0x00204060u);
  core.mem_w32(kGlows + 0x10u, 0x100u);
  core.mem_w32(kGlows + 0x20u, (uint32_t)bias);
  core.rsub.projParams.setGeomOffset(160, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

void test_a_three_point_ring_fans_two_triangles_from_the_centre() {
  const auto game = glowFixture();
  const auto recipe = spyro::glow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.records, 1u);
  CHECK_EQ(recipe.drawn, 1u);
  CHECK_EQ(recipe.faces.size(), (size_t)2);
  CHECK_EQ(recipe.rejects[(size_t)Reject::EmptyRecord], 15u);
  // Both triangles share the centre, and only the centre is coloured.
  CHECK_EQ(recipe.faces[0].vertices[0].sx, recipe.faces[1].vertices[0].sx);
  CHECK_EQ(recipe.faces[0].colour, 0x00204060u);
  // The fan is open, so the second triangle starts where the first ended.
  CHECK_EQ(recipe.faces[0].vertices[2].sx, recipe.faces[1].vertices[1].sx);
  CHECK_EQ(recipe.faces[0].vertices[2].sy, recipe.faces[1].vertices[1].sy);
  CHECK_EQ(recipe.faces[0].fanOrdinal, 0u);
  CHECK_EQ(recipe.faces[1].fanOrdinal, 1u);
}

// The ring is a screen-space offset scaled by radius/depth, not a world position, so halving the
// distance has to double the offset from the centre.
void test_the_ring_radius_scales_with_depth() {
  const auto near = glowFixture(3, 0x400);
  const auto far = glowFixture(3, 0x800);
  const auto nearRecipe = spyro::glow_recipe::derive(&near->core);
  const auto farRecipe = spyro::glow_recipe::derive(&far->core);
  CHECK(nearRecipe.status == Status::Ready);
  CHECK(farRecipe.status == Status::Ready);
  const int nearOffset = nearRecipe.faces[0].vertices[1].sx - nearRecipe.faces[0].vertices[0].sx;
  const int farOffset = farRecipe.faces[0].vertices[1].sx - farRecipe.faces[0].vertices[0].sx;
  CHECK(nearOffset != 0);
  CHECK_EQ(nearOffset, farOffset * 2);
}

void test_a_zero_point_count_is_an_empty_record_not_a_refusal() {
  const auto game = glowFixture(0);
  const auto recipe = spyro::glow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.records, 0u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::EmptyRecord], 16u);
}

// The record's bias moves the whole glow toward the viewer, and a bias past the centre's own bin
// drops the record instead of sorting it in front of the ordering table.
void test_a_bias_past_the_front_of_the_table_drops_the_record() {
  const auto game = glowFixture(3, 0x800, -0x20);
  const auto recipe = spyro::glow_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.records, 1u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::NegativeBin], 1u);
}

void test_outcode_names_each_screen_edge_separately() {
  CHECK_EQ(spyro::glow_recipe::outcode(100, 100), 0u);
  CHECK_EQ(spyro::glow_recipe::outcode(100, 1), 1u);
  CHECK_EQ(spyro::glow_recipe::outcode(100, 0x100), 2u);
  CHECK_EQ(spyro::glow_recipe::outcode(0x200, 100), 4u);
  CHECK_EQ(spyro::glow_recipe::outcode(0, 100), 8u);
}

// The far half of the table is stretched by 0x40 bins past 0xFF and stops at the last bin, so two
// distant glows cannot both pile into the final bucket by accident.
void test_ot_bin_steps_past_the_near_half_and_clamps_at_the_last_bin() {
  CHECK_EQ(spyro::glow_recipe::otBin(0x800u, 0), 0x10);
  CHECK_EQ(spyro::glow_recipe::otBin(0x8000u, 0), 0x140);
  CHECK_EQ(spyro::glow_recipe::otBin(0x8080u, 0), 0x141);
  CHECK_EQ(spyro::glow_recipe::otBin(0x100000u, 0), 0x7FF);
  CHECK_EQ(spyro::glow_recipe::otBin(0x800u, -0x10), 0);
}

} // namespace

int main() {
  RUN(a_three_point_ring_fans_two_triangles_from_the_centre);
  RUN(the_ring_radius_scales_with_depth);
  RUN(a_zero_point_count_is_an_empty_record_not_a_refusal);
  RUN(a_bias_past_the_front_of_the_table_drops_the_record);
  RUN(outcode_names_each_screen_edge_separately);
  RUN(ot_bin_steps_past_the_near_half_and_clamps_at_the_last_bin);
  return pt_summary();
}
