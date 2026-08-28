#include "game.h"
#include "game_iface.h"
#include "platform_hle.h"
#include "spyro2_runtime.h"

#include <concepts>
#include <memory>

namespace spyro2 {
void installRecompSubstrate() {}
} // namespace spyro2

static_assert(std::derived_from<spyro2::Spyro2Runtime, spyro::SpyroRuntime>);
static_assert(std::derived_from<spyro::SpyroRuntime, GameRuntime>);

int main() {
  spyro2::Spyro2Runtime unavailable;
  if (unavailable.installSubstrate() || unavailable.substrateRefusal().empty()) {
    return 1;
  }

  spyro2::Spyro2Runtime runtime(spyro2::installRecompSubstrate);
  const GuestProgramImage *image = runtime.guestProgramImage();
  const PlatformHlePlan *platform = runtime.platformHlePlan();
  const RenderCapabilities capabilities = runtime.renderCapabilities();
  psxport_install_game(runtime);
  auto game = std::make_unique<Game>();
  if (image == nullptr || runtime.legacyConfigForMigration() != nullptr ||
      runtime.legacyHooksForMigration() != nullptr || !runtime.installSubstrate() ||
      platform == nullptr || platform->vsyncAddress != 0x80058EDCu ||
      platform->windowLo[0] != 0x80058EDCu || platform->windowHi[0] != 0x80059054u ||
      capabilities.defaultPath != RenderPath::Gte || capabilities.nativeRenderPath ||
      capabilities.temporalInterpolation || !runtime.guestVramIsPicture(*game)) {
    return 1;
  }
  return runtime.title() == spyro::SpyroTitle::Spyro2 && image->bss.begin == 0x80066ED8u &&
                 image->bss.end == 0x8006D264u && image->globalPointer == 0x80066D38u &&
                 image->libcInitEntry == 0x8005ABD8u && image->gameMainEntry == 0x80011ADCu &&
                 image->crt0Entry == 0x8005478Cu && image->residentText.begin == 0x00010000u &&
                 image->residentText.end == 0x00067000u && image->stackBias.declared &&
                 image->stackBias.bytes == -8
             ? 0
             : 1;
}
