#include "core.h"
#include "field_shadow_recipe.h"
#include "testutil.h"

#include <memory>

namespace {

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
  RUN(retained_ot_formula);
  RUN(retained_radius_interpolation);
  RUN(missing_game_is_refused);
  return pt_summary();
}
