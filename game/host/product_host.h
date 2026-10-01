#pragma once

#include "title_availability.h"

#include <filesystem>

namespace spyro {

// The product's top level: the title selector, then the chosen title, then the selector again, all
// in this one process. An explicit executable (a maintainer override) skips the selector and runs
// that one title, which is the only way the process runs a single title.
class ProductHost {
public:
  explicit ProductHost(std::filesystem::path provisioningRoot);

  int runSelector();
  int runExecutable(const std::filesystem::path &executable);

private:
  std::filesystem::path root_;
};

} // namespace spyro
