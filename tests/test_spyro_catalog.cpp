// Spyro's catalog: one identity per title, in title order, each answering with its own runtime.
#include "title_runtime_registry.h"

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;

void expect(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
} // namespace

int main() {
  const spyro::SpyroCatalog catalog;
  const auto titles = catalog.titles();
  expect(catalog.productName() == "Spyro", "the product is named Spyro");
  expect(titles.size() == 3, "three titles");
  const spyro::SpyroTitle order[] = {
      spyro::SpyroTitle::Spyro1, spyro::SpyroTitle::Spyro2, spyro::SpyroTitle::Spyro3};
  const char *slugs[] = {"spyro1", "spyro2", "spyro3"};
  for (std::size_t index = 0; index < titles.size(); ++index) {
    expect(titles[index].slug == slugs[index], "catalog entries are in title order");
    expect(catalog.title(index) == order[index], "each entry carries its SpyroTitle");
    expect(catalog.runtime(index).title() == order[index], "each entry answers its own runtime");
  }
  std::printf("spyro catalog: %d failure(s)\n", failures);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
