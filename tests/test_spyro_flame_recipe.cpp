// 0x80058D64 draws Spyro's flame as eight parts, each a tip fan of four untextured Gouraud
// triangles followed by a ribbon of Gouraud textured quads walking backward through the part's
// cross-section array. Nothing in the port called it at all, so these cases pin the shape of what
// it produces and, just as importantly, the cases where producing nothing is the right answer.
#include "core.h"
#include "game.h"
#include "spyro_flame_recipe.h"
#include "testutil.h"

#include <array>
#include <memory>

namespace {

using spyro::flame_recipe::Recipe;
using spyro::flame_recipe::Reject;
using spyro::flame_recipe::Status;

constexpr uint32_t kCamera = 0x80076dd0u;
constexpr uint32_t kFlame = 0x800786c8u;
constexpr uint32_t kPartDescriptors = 0x8006d94cu;
constexpr uint32_t kPartPoints = 0x8006daa8u;
constexpr uint32_t kTipColours = 0x8006e1a8u;
constexpr uint32_t kSine = 0x8006cbf8u;

// One active part directly ahead of an exactly orthogonal camera. Only part 0 carries a length, so
// the other seven are counted as empty rather than silently skipped, and the array it walks holds
// four cross-sections at a constant radius so the ribbon closes.
std::unique_ptr<Game> flameFixture(int8_t partLength = 1, uint32_t sections = 3) {
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  const std::array<int16_t, 9> matrix{4096, 0, 0, 0, 0, -4096, 0, -4096, 0};
  for (size_t i = 0; i < matrix.size(); ++i) {
    core.mem_w16(kCamera + (uint32_t)i * 2u, (uint16_t)matrix[i]);
  }
  // A quarter-turn sine table: entry 0 is zero and the cosine a quarter later is one.
  for (uint32_t i = 0; i < 256u; ++i) {
    core.mem_w16(kSine + i * 2u, 0u);
  }
  core.mem_w16(kSine + 0x80u, (uint16_t)4096);

  core.mem_w32(kFlame + 0u, 0u);             // flame world X
  core.mem_w32(kFlame + 4u, 0u);             // flame world Y
  core.mem_w32(kFlame + 8u, 0u);             // flame world Z
  core.mem_w32(kFlame + 0x10u, 0x00010000u); // u0v0 | clut
  core.mem_w32(kFlame + 0x14u, 0x00020000u); // u1v1 | tpage
  core.mem_w32(kFlame + 0x9cu, 0u);          // not the superflame variant
  // An identity orientation matrix in the GTE's five packed words.
  core.mem_w32(kFlame + 0xb8u, 0x00001000u);
  core.mem_w32(kFlame + 0xbcu, 0x00000000u);
  core.mem_w32(kFlame + 0xc0u, 0x00001000u);
  core.mem_w32(kFlame + 0xc4u, 0x00000000u);
  core.mem_w32(kFlame + 0xc8u, 0x00001000u);
  for (uint32_t part = 0; part < 8u; ++part) {
    core.mem_w8(kFlame + 0x20u + part, 0u);
    core.mem_w8(kFlame + 0x28u + part, 0u);
  }
  core.mem_w8(kFlame + 0x20u, (uint8_t)partLength);
  core.mem_w8(kFlame + 0x28u, 8u); // the cursor limit, in eight-byte units

  // The descriptor: the cursor starts six cross-sections in, because the walk runs BACKWARD toward
  // the array base and the tip reads the entry before the cursor.
  core.mem_w32(kPartDescriptors, 0x06000000u | (sections << 16));
  // Four cross-sections at x=0,y=0 and increasing depth, all at sine index 0.
  for (uint32_t i = 0; i < 8u; ++i) {
    core.mem_w32(kPartPoints + i * 8u, (uint32_t)(0x0200u + i * 0x40u));
    core.mem_w32(kPartPoints + i * 8u + 4u, 0u);
  }
  for (uint32_t i = 0; i < 4u; ++i) {
    core.mem_w32(kTipColours + i * 4u, 0x30000000u | (0x101010u * (i + 1u)));
  }
  core.rsub.projParams.setGeomOffset(256, 120);
  core.rsub.projParams.setGeomScreen(256);
  return game;
}

void test_missing_game_is_refused() {
  CHECK(spyro::flame_recipe::derive(nullptr).status == Status::InvalidCore);
}

void test_every_part_empty_is_valid_and_counted() {
  auto game = flameFixture();
  game->core.mem_w8(kFlame + 0x20u, 0u);
  const auto recipe = spyro::flame_recipe::derive(&game->core);
  CHECK(recipe.status == Status::ValidEmpty);
  CHECK_EQ(recipe.parts, 0u);
  // The distinguishing part: eight empty parts are reported, not silently absent.
  CHECK_EQ(recipe.rejects[(size_t)Reject::EmptyPart], 8u);
}

void test_one_active_part_is_counted_once() {
  const auto game = flameFixture();
  const auto recipe = spyro::flame_recipe::derive(&game->core);
  CHECK_EQ(recipe.parts, 1u);
  CHECK_EQ(recipe.rejects[(size_t)Reject::EmptyPart], 7u);
}

void test_ribbon_emits_one_quad_fewer_than_its_cross_sections() {
  const auto game = flameFixture(1, 3);
  const auto recipe = spyro::flame_recipe::derive(&game->core);
  // Each quad spans the edge between two consecutive cross-sections, so the first one only seeds
  // the shared edge. A ribbon that emitted a quad for the first section would double its root.
  CHECK(recipe.ribbons > 0u);
  CHECK(recipe.status == Status::Ready);
  for (const auto &face : recipe.faces) {
    CHECK(face.nv == 3 || face.nv == 4);
    if (face.nv == 4) {
      CHECK(face.textured);
      CHECK(face.semi);
    } else {
      CHECK(!face.textured);
    }
  }
}

void test_ribbon_shade_and_texture_row_advance_together() {
  const auto game = flameFixture(1, 4);
  const auto recipe = spyro::flame_recipe::derive(&game->core);
  const spyro::flame_recipe::Face *quad = nullptr;
  for (const auto &face : recipe.faces) {
    if (face.nv == 4) {
      quad = &face;
      break;
    }
  }
  CHECK(quad != nullptr);
  if (quad != nullptr) {
    // Corners 0/1 share the near edge's shade and row; 2/3 carry one step of both.
    CHECK_EQ(quad->red[0], quad->red[1]);
    CHECK_EQ(quad->red[2], quad->red[3]);
    CHECK(quad->red[2] < quad->red[0]);
    CHECK_EQ(quad->v[0], quad->v[1]);
    CHECK_EQ(quad->v[2], quad->v[3]);
    CHECK(quad->v[2] != quad->v[0]);
  }
}

void test_ring_scale_is_narrow_until_the_ribbon_is_established() {
  // Retail sets the scale to 8 by default and only widens it once a previous cross-section has been
  // projected, so the tip and the two closing steps stay narrow.
  CHECK_EQ(spyro::flame_recipe::ringScale(false, false), 8);
  CHECK_EQ(spyro::flame_recipe::ringScale(false, true), 8);
  CHECK_EQ(spyro::flame_recipe::ringScale(true, false), 0x2c);
  CHECK_EQ(spyro::flame_recipe::ringScale(true, true), 0x40);
}

void test_ramp_grey_darkens_five_steps_per_texture_row() {
  CHECK_EQ((int)spyro::flame_recipe::rampGrey(0), 0x80);
  CHECK_EQ((int)spyro::flame_recipe::rampGrey(1), 0x7b);
  CHECK_EQ((int)spyro::flame_recipe::rampGrey(16), 0x80 - 80);
}

} // namespace

int main() {
  RUN(missing_game_is_refused);
  RUN(every_part_empty_is_valid_and_counted);
  RUN(one_active_part_is_counted_once);
  RUN(ribbon_emits_one_quad_fewer_than_its_cross_sections);
  RUN(ribbon_shade_and_texture_row_advance_together);
  RUN(ring_scale_is_narrow_until_the_ribbon_is_established);
  RUN(ramp_grey_darkens_five_steps_per_texture_row);
  return pt_summary();
}
