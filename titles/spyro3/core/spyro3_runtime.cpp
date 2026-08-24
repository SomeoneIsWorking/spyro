#include "spyro3_runtime.h"

#include <lucent/log.h>

#include <cstdlib>

namespace spyro3 {

const GuestProgramImage Spyro3Runtime::programImage_{
    .bss = {0x8006C4F4u, 0x800742D0u},
    .stackTopWordAddress = 0x8006C3E4u,
    .stackReserveWordAddress = 0x8006C3E0u,
    .heapBase = 0x800742D0u,
    .heapSizeStoreAddress = 0x80069F04u,
    .heapBaseStoreAddress = 0x80069F00u,
    .globalPointer = 0x8006C3B0u,
    .libcInitEntry = 0x8005F63Cu,
    .gameMainEntry = 0x8001200Cu,
    .crt0Entry = 0x80059444u,
    .residentText = {0x00010000u, 0x0006C800u},
    .backtraceText = {},
    .stackBias = {true, -8},
};

Spyro3Runtime::Spyro3Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro3) {}

void *Spyro3Runtime::createContext(Core &) {
  return nullptr;
}

void Spyro3Runtime::destroyContext(void *) {}

void Spyro3Runtime::registerOverrides(Game &) {}

bool Spyro3Runtime::installSubstrate() {
  return false;
}

std::string_view Spyro3Runtime::substrateRefusal() const {
  return "SCUS_944.67 is measured through crt0 and its first game-main call, but has no generated "
         "substrate; execution is refused before Game construction";
}

void Spyro3Runtime::bootInit(Core &) {
  lucent::error("spyro3-runtime",
                "unreachable: substrate refusal must happen before Game construction");
  std::abort();
}

bool Spyro3Runtime::guestVramIsPicture(const Game &) const {
  lucent::error("spyro3-runtime", "{}", substrateRefusal());
  std::abort();
}

} // namespace spyro3
