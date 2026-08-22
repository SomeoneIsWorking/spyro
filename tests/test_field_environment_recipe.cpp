#include "field_environment_recipe.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "field_environment_recipe: " << what << '\n';
    std::exit(1);
  }
}

void check(spyro::field_environment::State state,
           spyro::field_environment::Invocation expected,
           const char *what) {
  require(spyro::field_environment::derive(state) == expected, what);
}

} // namespace

int main() {
  using spyro::field_environment::Invocation;
  using spyro::field_environment::State;
  check({4, 5, 0}, {4, 0x28000u}, "valid camera occlusion group");
  check({4, 4, 0}, {-1, 0x14000u}, "equal group count falls back");
  check({8, 4, 13}, {-1, 0x1c000u}, "title fallback distance");
  check({8, 4, 14}, {-1, 0x1c000u}, "cutscene fallback distance");
  check({8, 4, 12}, {-1, 0x14000u}, "pre-title fallback distance");
  check({8, 4, 15}, {-1, 0x14000u}, "credits fallback distance");
  check({-1, 4, 0}, {-1, 0x28000u}, "signed camera group comparison");
  check({1, 4, 13}, {1, 0x28000u}, "occlusion group outranks stage distance");

  const Invocation expected = spyro::field_environment::derive({4, 5, 0});
  using spyro::field_environment::matches;
  require(matches(expected, {4, 0x28000u, 0}), "matching boundary rejected");
  require(!matches(expected, {4, 0x14000u, 0}), "changed distance was not detected");
  require(!matches(expected, {-1, 0x28000u, 0}), "changed selection was not detected");
  require(!matches(expected, {4, 0x28000u, 1}), "uncleared work byte was not detected");
  std::cout << "field_environment_recipe: PASS (8 branch cases + 3 boundary negatives)\n";
  return 0;
}
