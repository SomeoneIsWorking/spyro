#include "screen_fade_recipe.h"
#include "testutil.h"

namespace {

void test_absent_fade_is_valid_empty() {
  const auto recipe = spyro::screen_fade_recipe::cutscene(0u, 0, 240, 684);
  CHECK(!recipe.visible);
}

void test_cutscene_fade_uses_guest_colour_and_wide_extent() {
  const auto recipe = spyro::screen_fade_recipe::cutscene(7u, 0, 240, 684);
  CHECK(recipe.visible);
  CHECK_EQ(recipe.x0, 0);
  CHECK_EQ(recipe.x1, 684);
  CHECK_EQ(recipe.y0, 248);
  CHECK_EQ(recipe.y1, 472);
  CHECK_EQ(recipe.r, 0x70u);
  CHECK_EQ(recipe.g, 0x70u);
  CHECK_EQ(recipe.b, 0x70u);
  CHECK_EQ(recipe.blendMode, 2u);
}

void test_guest_byte_colour_wrap_is_preserved() {
  const auto recipe = spyro::screen_fade_recipe::cutscene(0x12u, 0, 0, 512);
  CHECK_EQ(recipe.r, 0x20u);
}

} // namespace

int main() {
  RUN(absent_fade_is_valid_empty);
  RUN(cutscene_fade_uses_guest_colour_and_wide_extent);
  RUN(guest_byte_colour_wrap_is_preserved);
  return pt_summary();
}
