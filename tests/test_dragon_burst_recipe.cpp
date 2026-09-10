// 0x80058864 is the burst the dragon cutscene draws over every one of its states. It builds an
// eight-spoke star out of two sine-table rings around one projected origin, and it is the reason
// GS_Dragon could not be composed from the already-owned producers alone.
#include "core.h"
#include "dragon_burst_recipe.h"
#include "game.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::dragon_burst::Status;

constexpr uint32_t kBurst = 0x80076248u;
constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kSine = 0x8006cbf8u;

// The burst directly ahead of an identity camera. The sine table is filled with a real quarter-turn
// shape so the two rings land at different radii rather than all collapsing onto one point.
std::unique_ptr<Game>
burstFixture(uint32_t enable = 1, int32_t depth = 0x4000, int32_t radius = 0x200) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  const std::array<uint32_t, 5> identity{
      0x00001000u, 0x00000000u, 0x00001000u, 0x00000000u, 0x00001000u};
  for (uint32_t i = 0; i < identity.size(); ++i) {
    core.mem_w32(kCamera + i * 4u, identity[i]);
    core.mem_w32(kBurst + 0x10u + i * 4u, identity[i]);
  }
  core.mem_w32(kCamera + 0x28u, (uint32_t)(-depth));
  core.mem_w32(kCamera + 0x2cu, 0u);
  core.mem_w32(kCamera + 0x30u, 0u);
  // A full table: entry n is 4096*sin(2*pi*n/256) rounded, which is what the guest ships.
  for (uint32_t i = 0; i < 512u; ++i) {
    const double angle = 6.283185307179586 * (double)i / 256.0;
    core.mem_w16(kSine + i * 2u, (uint16_t)(int16_t)(int)(4096.0 * __builtin_sin(angle)));
  }
  core.mem_w32(kBurst + 0x00u, enable);
  core.mem_w32(kBurst + 0x04u, 0u);
  core.mem_w32(kBurst + 0x08u, 0u);
  core.mem_w32(kBurst + 0x0cu, 0u);
  core.mem_w32(kBurst + 0x24u, (uint32_t)radius);
  core.mem_w8(kBurst + 0x2bu, 0x60u);
  core.rsub.projParams.setGeomOffset(160, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

void test_missing_core_is_refused() {
  CHECK(spyro::dragon_burst::derive(nullptr).status == Status::InvalidCore);
}

void test_a_disarmed_gate_is_an_empty_frame_not_a_refusal() {
  const auto game = burstFixture(0);
  const auto recipe = spyro::dragon_burst::derive(&game->core);
  CHECK(recipe.status == Status::Inactive);
  CHECK_EQ(recipe.triangles.size(), (size_t)0);
}

void test_eight_spokes_make_sixteen_triangles() {
  const auto game = burstFixture();
  const auto recipe = spyro::dragon_burst::derive(&game->core);
  CHECK(recipe.status == Status::Ready);
  // Two per spoke: the outer point, then the fan back to the centre.
  CHECK_EQ(recipe.triangles.size(), (size_t)16);
  CHECK_EQ((int)recipe.colour, 0x60);
}

void test_the_ring_closes_across_its_seam() {
  const auto game = burstFixture();
  const auto recipe = spyro::dragon_burst::derive(&game->core);
  // Retail copies the LAST inner point in front of the first, so spoke 0 spans the seam. Without
  // that copy the star would be missing one of its eight wedges and still look like a star.
  const auto &seam = recipe.triangles[0].vertices;
  const auto &last = recipe.triangles[14].vertices;
  CHECK_EQ(seam[0].sx, last[1].sx);
  CHECK_EQ(seam[0].sy, last[1].sy);
}

void test_every_pair_of_triangles_shares_its_inner_edge() {
  const auto game = burstFixture();
  const auto recipe = spyro::dragon_burst::derive(&game->core);
  for (size_t spoke = 0; spoke < spyro::dragon_burst::kSpokes; ++spoke) {
    const auto &outerTri = recipe.triangles[spoke * 2].vertices;
    const auto &centreTri = recipe.triangles[spoke * 2 + 1].vertices;
    CHECK_EQ(outerTri[0].sx, centreTri[0].sx);
    CHECK_EQ(outerTri[1].sx, centreTri[1].sx);
    CHECK_EQ(outerTri[0].sy, centreTri[0].sy);
    CHECK_EQ(outerTri[1].sy, centreTri[1].sy);
  }
  // Every fan triangle ends at the same centre point.
  for (size_t spoke = 1; spoke < spyro::dragon_burst::kSpokes; ++spoke) {
    CHECK_EQ(recipe.triangles[spoke * 2 + 1].vertices[2].sx, recipe.triangles[1].vertices[2].sx);
  }
}

void test_the_outer_ring_reaches_further_than_the_inner_one() {
  const auto game = burstFixture();
  const auto recipe = spyro::dragon_burst::derive(&game->core);
  const auto centre = recipe.triangles[1].vertices[2];
  int32_t innerReach = 0;
  int32_t outerReach = 0;
  for (size_t spoke = 0; spoke < spyro::dragon_burst::kSpokes; ++spoke) {
    const auto &inner = recipe.triangles[spoke * 2].vertices[1];
    const auto &outer = recipe.triangles[spoke * 2].vertices[2];
    innerReach = std::max(innerReach, (int32_t)std::abs(inner.sx - centre.sx));
    outerReach = std::max(outerReach, (int32_t)std::abs(outer.sx - centre.sx));
  }
  // The outer ring is shifted two bits less, so it must reach measurably further. A decode that
  // used the same shift for both would produce a disc and still pass every count above.
  CHECK(innerReach > 0);
  CHECK(outerReach > innerReach);
}

} // namespace

int main() {
  RUN(missing_core_is_refused);
  RUN(a_disarmed_gate_is_an_empty_frame_not_a_refusal);
  RUN(eight_spokes_make_sixteen_triangles);
  RUN(the_ring_closes_across_its_seam);
  RUN(every_pair_of_triangles_shares_its_inner_edge);
  RUN(the_outer_ring_reaches_further_than_the_inner_one);
  return pt_summary();
}
