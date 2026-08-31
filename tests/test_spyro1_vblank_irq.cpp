#include "spyro1_vblank_irq.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "spyro1_vblank_irq: %s\n", what);
    std::exit(1);
  }
}

void testOnlyEnabledVblankSelectsTheIrqPath() {
  require(!spyro1::hasPendingEnabledVblank(0u, 1u), "clear I_STAT selected the IRQ path");
  require(!spyro1::hasPendingEnabledVblank(1u, 0u), "masked VBlank selected the IRQ path");
  require(spyro1::hasPendingEnabledVblank(1u, 1u), "enabled VBlank did not select the IRQ path");
}

void testHookEntryIntIsRequiredForIrqDelivery() {
  require(!spyro1::shouldDispatchVblankThroughIrq(1u, 1u, 0u),
          "VBlank without HookEntryInt selected the IRQ path");
  require(spyro1::shouldDispatchVblankThroughIrq(1u, 1u, 0x8007395Cu),
          "enabled VBlank with HookEntryInt did not select the IRQ path");
}

void testOtherPendingSourcesDoNotSelectTheVblankPath() {
  constexpr std::uint32_t kCdIrq = 1u << 2;
  require(!spyro1::hasPendingEnabledVblank(kCdIrq, kCdIrq),
          "a non-VBlank IRQ selected the VBlank path");
  require(spyro1::hasPendingEnabledVblank(kCdIrq | 1u, kCdIrq | 1u),
          "a concurrent enabled VBlank was hidden by another IRQ source");
}

} // namespace

int main() {
  testOnlyEnabledVblankSelectsTheIrqPath();
  testHookEntryIntIsRequiredForIrqDelivery();
  testOtherPendingSourcesDoNotSelectTheVblankPath();
  std::puts("spyro1_vblank_irq: PASS (only enabled I_STAT bit 0 uses IRQ delivery)");
  return 0;
}
