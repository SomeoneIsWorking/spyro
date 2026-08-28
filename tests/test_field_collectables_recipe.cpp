#include "field_collectables_recipe.h"
#include "testutil.h"

namespace {

using spyro::field_collectables_recipe::State;
using spyro::field_collectables_recipe::Status;

void test_hud_mobys_follow_retail_slot_order() {
  State state{};
  state.gemDisplay = 1;
  state.dragonDisplay = 1;
  state.lifeDisplay = 1;
  state.keyDisplay = 1;
  state.keyFlag = 1;
  const auto recipe = spyro::field_collectables_recipe::derive(state);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.shadedCount, 12u);
  for (uint32_t i = 0; i < recipe.shadedCount; ++i) {
    CHECK_EQ(recipe.shadedMobys[i], 0x80077fecu + i * 0x58u);
  }
}

void test_life_orb_and_egg_ft4_recipes() {
  State state{};
  state.lifeDisplay = 1;
  state.eggDisplay = 1;
  state.lifeOrbCount = 2;
  state.eggCount = 2;
  state.eggPhase = 8;
  state.specularTime = 13;
  state.cosine[13] = 0x1000;
  state.cosine[1] = -0x1000;
  state.rects[12] = {10, 20, 8, 9};
  state.rects[13] = {30, 40, 10, 11};
  state.rects[0] = {50, 60, 12, 13};
  state.rects[1] = {70, 80, 14, 15};
  state.tiles[0] = {1, 2, 0x2420, 0x0080};
  state.tiles[9] = {3, 4, 0x2421, 0x0081};
  state.tiles[1] = {5, 6, 0x2422, 0x0082};
  const auto recipe = spyro::field_collectables_recipe::derive(state);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.spriteCount, 4u);
  CHECK_EQ(recipe.sprites[0].r, 160u);
  CHECK_EQ(recipe.sprites[1].r, 96u);
  CHECK_EQ(recipe.sprites[2].tile.u, 3u);
  CHECK_EQ(recipe.sprites[3].tile.u, 5u);
  CHECK_EQ(recipe.sprites[2].r, 0x80u);
}

void test_life_phase_uses_floor_of_fractional_step() {
  State state{};
  state.lifeDisplay = 1;
  state.lifeOrbCount = 3;
  state.specularTime = 30;
  state.cosine[5] = 0x0800;
  const auto recipe = spyro::field_collectables_recipe::derive(state);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.spriteCount, 3u);
  // i=2 subtracts floor(2*256/20)=25, not 2*floor(256/20)=24.
  CHECK_EQ(recipe.sprites[2].r, 144u);
}

void test_disarmed_and_negative_counts_are_empty() {
  State state{};
  state.lifeOrbCount = 21;
  state.eggCount = 13;
  CHECK(spyro::field_collectables_recipe::derive(state).status == Status::Ready);
  state.lifeDisplay = 1;
  state.eggDisplay = 1;
  state.lifeOrbCount = -1;
  state.eggCount = -1;
  const auto recipe = spyro::field_collectables_recipe::derive(state);
  CHECK(recipe.status == Status::Ready);
  CHECK_EQ(recipe.spriteCount, 0u);
}

void test_unowned_text_and_impossible_counts_refuse_atomically() {
  State completed{};
  completed.gemDisplay = 4;
  CHECK(spyro::field_collectables_recipe::derive(completed).status ==
        Status::CompletedGemTextUnowned);
  State overflow{};
  overflow.lifeDisplay = 1;
  overflow.lifeOrbCount = 21;
  CHECK(spyro::field_collectables_recipe::derive(overflow).status == Status::InvalidCount);
}

} // namespace

int main() {
  RUN(hud_mobys_follow_retail_slot_order);
  RUN(life_orb_and_egg_ft4_recipes);
  RUN(life_phase_uses_floor_of_fractional_step);
  RUN(disarmed_and_negative_counts_are_empty);
  RUN(unowned_text_and_impossible_counts_refuse_atomically);
  return pt_summary();
}
