#include "product_host.h"

#include <cstring>
#include <lucent/log.h>

namespace {
constexpr const char *kProvisioningRoot = "scratch/assets";

bool helpRequested(int argc, char **argv) {
  return argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0);
}

void printUsage(const char *program) {
  lucent::info(
      "cli",
      "Usage: {} [executable]\n"
      "With no argument: the title selector, then the chosen Spyro title, in this process.\n"
      "With an executable: run exactly that serial-identified executable (maintainer override).",
      program);
}
} // namespace

int main(int argc, char **argv) {
  if (helpRequested(argc, argv)) {
    printUsage(argv[0]);
    return 0;
  }
  spyro::ProductHost host(kProvisioningRoot);
  return argc > 1 ? host.runExecutable(argv[1]) : host.runSelector();
}
