// test_hud_layout.cpp — which part of the guest's HUD a Moby record is, and its anchor class.
#include "hud_layout.h"
#include "testutil.h"

namespace {

using spyro::hud_layout::mobyPart;
using spyro::ui_anchor::Anchor;

constexpr std::uint32_t moby(std::uint32_t index) {
  return spyro::hud_layout::kMobys + index * spyro::hud_layout::kMobySize;
}

void test_every_hud_moby_has_the_class_its_authored_side_gives_it() {
  for (std::uint32_t i = 0; i <= 4u; ++i) {
    CHECK(mobyPart(moby(i)).value().anchor == Anchor::LeftEdge);
  }
  for (std::uint32_t i = 5u; i <= 7u; ++i) {
    CHECK(mobyPart(moby(i)).value().anchor == Anchor::Centred);
  }
  for (std::uint32_t i = 8u; i <= 11u; ++i) {
    CHECK(mobyPart(moby(i)).value().anchor == Anchor::RightEdge);
  }
  CHECK_EQ(mobyPart(moby(10)).value().index, 10u);
}

void test_anything_that_is_not_one_of_the_twelve_is_not_a_hud_part() {
  CHECK(!mobyPart(0u).has_value());
  CHECK(!mobyPart(moby(0) - spyro::hud_layout::kMobySize).has_value());
  CHECK(!mobyPart(moby(12)).has_value());
  // Inside the array but not on a record boundary: an address that is not a Moby at all.
  CHECK(!mobyPart(moby(3) + 4u).has_value());
}

void test_the_two_sprite_families_are_on_opposite_sides() {
  CHECK(spyro::hud_layout::kTreasureRowAnchor == Anchor::LeftEdge);
  CHECK(spyro::hud_layout::kLifeOrbAnchor == Anchor::RightEdge);
}

} // namespace

int main() {
  RUN(every_hud_moby_has_the_class_its_authored_side_gives_it);
  RUN(anything_that_is_not_one_of_the_twelve_is_not_a_hud_part);
  RUN(the_two_sprite_families_are_on_opposite_sides);
  return pt_summary();
}
