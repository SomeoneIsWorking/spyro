#include "spyro3_runtime.h"

#include <concepts>

static_assert(std::derived_from<spyro3::Spyro3Runtime, spyro::SpyroRuntime>);

int main() {
  spyro3::Spyro3Runtime runtime;
  const GuestProgramImage *image = runtime.guestProgramImage();
  if (image == nullptr || runtime.legacyConfigForMigration() != nullptr ||
      runtime.legacyHooksForMigration() != nullptr || runtime.installSubstrate()) {
    return 1;
  }
  return runtime.title() == spyro::SpyroTitle::Spyro3 && image->bss.begin == 0x8006C4F4u &&
                 image->bss.end == 0x800742D0u && image->globalPointer == 0x8006C3B0u &&
                 image->libcInitEntry == 0x8005F63Cu && image->gameMainEntry == 0x8001200Cu &&
                 image->crt0Entry == 0x80059444u && image->residentText.begin == 0x00010000u &&
                 image->residentText.end == 0x0006C800u && image->stackBias.declared &&
                 image->stackBias.bytes == -8
             ? 0
             : 1;
}
