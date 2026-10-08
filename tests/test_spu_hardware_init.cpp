#include "spu_hardware_init.h"

#include <cstdio>
#include <cstdlib>

namespace {

void require(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "spu_hardware_init: %s\n", what);
    std::abort();
  }
}

} // namespace

int main() {
  require(spyro::spuHardwareNeedsFullReset(0u), "mode zero no longer selects the full reset");
  require(!spyro::spuHardwareNeedsFullReset(1u) && !spyro::spuHardwareNeedsFullReset(0xFFFFFFFFu),
          "a nonzero mode incorrectly selects the full-reset-only register sequence");

  constexpr spyro::SpuVoiceReset reset = spyro::spuVoiceReset();
  require(reset.volumeLeft == 0u && reset.volumeRight == 0u && reset.pitch == 0x3FFFu &&
              reset.startAddress == 0x200u && reset.adsr1 == 0u && reset.adsr2 == 0u,
          "24-voice reset tuple differs from SCUS_942.28 0x8005BDA8");
  require(spyro::kSpuVoiceCount == 24u && spyro::kSpuPendingPitchCount == 10u,
          "SPU reset loop bounds differ from the executable");
  return 0;
}
