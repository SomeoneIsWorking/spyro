#include "spyro3_runtime.h"

#include <concepts>

static_assert(std::derived_from<spyro3::Spyro3Runtime, spyro::SpyroRuntime>);

int main() {
  spyro3::Spyro3Runtime runtime;
  const GuestProgramImage *image = runtime.guestProgramImage();
  const RenderCapabilities capabilities = runtime.renderCapabilities();
  if (image == nullptr || runtime.legacyConfigForMigration() != nullptr ||
      runtime.legacyHooksForMigration() != nullptr || capabilities.defaultPath != RenderPath::Gte ||
      capabilities.nativeRenderPath || capabilities.temporalInterpolation) {
    return 1;
  }
  // Every value below is read out of SCUS_944.67's own crt0 and initialised data, and each is
  // quoted in titles/spyro3/core/spyro3_runtime.cpp next to the instruction that produces it. The
  // four the earlier revision did not check are the ones a wrong value would break silently: the
  // stack top the crt0 loads at 0x8005946C (`lw $v0,-0x3c1c($v0)` -> 0x8006C3E4) and the heap plan
  // it stores at 0x800594A8 / 0x800594B4.
  return runtime.title() == spyro::SpyroTitle::Spyro3 && image->bss.begin == 0x8006C4F4u &&
                 image->bss.end == 0x800742D0u && image->globalPointer == 0x8006C3B0u &&
                 image->libcInitEntry == 0x8005F63Cu && image->gameMainEntry == 0x8001200Cu &&
                 image->crt0Entry == 0x80059444u && image->residentText.begin == 0x00010000u &&
                 image->residentText.end == 0x0006C800u && image->stackBias.declared &&
                 image->stackBias.bytes == -8 && image->stackTopWordAddress == 0x8006C3E4u &&
                 image->stackReserveWordAddress == 0x8006C3E0u &&
                 image->heapSizeStoreAddress == 0x80069F04u &&
                 image->heapBaseStoreAddress == 0x80069F00u && image->heapBase == 0x800742D0u
             ? 0
             : 1;
}
