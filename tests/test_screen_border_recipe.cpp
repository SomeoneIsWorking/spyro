#include "screen_border_recipe.h"
#include "testutil.h"

namespace {

void test_zero_height_without_enable_is_invisible() {
  const auto recipe = spyro::screen_border_recipe::field(0u, 0, 2, 0, 0, 512);
  CHECK(!recipe.visible);
  CHECK_EQ(recipe.barHeight, 0);
}

void test_enabled_ramp_steps_by_delta_time_and_holds_at_22() {
  // 20 + 2 = 22; the authored hold height.
  const auto up = spyro::screen_border_recipe::field(1u, 20, 2, 0, 0, 512);
  CHECK(up.visible);
  CHECK_EQ(up.barHeight, 22);
  CHECK_EQ(up.topY0, 0);
  CHECK_EQ(up.topY1, 22);
  CHECK_EQ(up.bottomY0, 218);
  CHECK_EQ(up.bottomY1, 240);
  // The <= 21 guard: a bar already at 22 must not grow past the hold, even at deltaTime 4.
  const auto held = spyro::screen_border_recipe::field(1u, 22, 4, 0, 0, 512);
  CHECK_EQ(held.barHeight, 22);
  // The >= 23 clamp: 21 + 4 overshoots the hold and is pinned back to 22.
  const auto overshot = spyro::screen_border_recipe::field(1u, 21, 4, 0, 0, 512);
  CHECK_EQ(overshot.barHeight, 22);
}

void test_disabled_ramp_steps_back_to_zero_without_crossing() {
  const auto down = spyro::screen_border_recipe::field(0u, 3, 2, 0, 0, 512);
  CHECK(down.visible);
  CHECK_EQ(down.barHeight, 1);
  // 1 - 2 crosses zero and is clamped, exactly once: the bars disappear cleanly.
  const auto gone = spyro::screen_border_recipe::field(0u, 1, 2, 0, 0, 512);
  CHECK(!gone.visible);
  CHECK_EQ(gone.barHeight, 0);
}

void test_bars_span_the_live_render_width() {
  // The native picture widens x to the live render width; the authored 0..240 vertical extent
  // and the bar height are the guest's own.
  const auto recipe = spyro::screen_border_recipe::field(1u, 22, 2, 86, 0, 684);
  CHECK(recipe.visible);
  CHECK_EQ(recipe.x0, 86);
  CHECK_EQ(recipe.x1, 770);
  CHECK_EQ(recipe.topY0, 0);
  CHECK_EQ(recipe.topY1, 22);
  CHECK_EQ(recipe.bottomY0, 218);
  CHECK_EQ(recipe.bottomY1, 240);
}

} // namespace

int main() {
  RUN(zero_height_without_enable_is_invisible);
  RUN(enabled_ramp_steps_by_delta_time_and_holds_at_22);
  RUN(disabled_ramp_steps_back_to_zero_without_crossing);
  RUN(bars_span_the_live_render_width);
  return pt_summary();
}
