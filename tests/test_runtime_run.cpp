#include "runtime_run.h"
#include <cstdlib>
#include <lucent/log.h>

int main() {
  spyro::RuntimeRun capped(2), independent(1), uncapped;
  if (capped.shouldEnd()) {
    return EXIT_FAILURE;
  }
  capped.fieldDelivered();
  if (capped.shouldEnd() || independent.fields() != 0) {
    return EXIT_FAILURE;
  }
  capped.fieldDelivered();
  if (!capped.shouldEnd() || capped.fields() != 2) {
    return EXIT_FAILURE;
  }
  for (unsigned i = 0; i < 10; ++i) {
    uncapped.fieldDelivered();
  }
  if (uncapped.shouldEnd()) {
    return EXIT_FAILURE;
  }
  uncapped.requestEnd();
  if (!uncapped.shouldEnd() || independent.shouldEnd()) {
    return EXIT_FAILURE;
  }
  lucent::info("test",
               "runtime observation: cap, uncapped, explicit end, and independent lifetimes: 4/4");
  return EXIT_SUCCESS;
}
