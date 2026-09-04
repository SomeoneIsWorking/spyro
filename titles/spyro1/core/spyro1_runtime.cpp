#include "spyro1_runtime.h"

#include "game.h"
#include "spyro_game.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro1 {

const GuestProgramImage Spyro1Runtime::programImage_{
    .bss = {0x80075640u, 0x8007AA38u},
    .stackTopWordAddress = 0x800755A8u,
    .stackReserveWordAddress = 0x800755A4u,
    .heapBase = 0x8007AA38u,
    .heapSizeStoreAddress = 0x800730C4u,
    .heapBaseStoreAddress = 0x800730C0u,
    .globalPointer = 0x80075264u,
    .libcInitEntry = 0x8005DB14u,
    .gameMainEntry = 0x80012204u,
    .crt0Entry = 0x8005B8E0u,
    .residentText = {0x00010000u, 0x00075800u},
    .backtraceText = {},
    .stackBias = {true, -8},
};

Spyro1Runtime::Spyro1Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro1) {}

void *Spyro1Runtime::createContext(Core &) {
  return nullptr;
}

void Spyro1Runtime::destroyContext(void *) {}

void Spyro1Runtime::registerOverrides(Game &game) {
  spyro_register_native_rand(game.core);
  spyro_register_native_leaves(game.core);
  spyro_register_native_vec(game.core);
  spyro_register_native_gte(game.core);
  spyro_register_native_angle(game.core);
  spyro_register_native_util(game.core);
  spyro_register_native_gameplay(game.core);
  lucent::info("boot", "installed Spyro 1's verified image-scoped native overrides");
}

void Spyro1Runtime::bootInit(Core &) {
  lucent::error("boot", "Spyro 1 native frame boot is not an entry path before JIT conformance");
  std::abort();
}

RenderCapabilities Spyro1Runtime::renderCapabilities() const {
  return RenderCapabilities::direct();
}

bool Spyro1Runtime::guestVramIsPicture(const Game &) const {
  return true;
}

} // namespace spyro1
