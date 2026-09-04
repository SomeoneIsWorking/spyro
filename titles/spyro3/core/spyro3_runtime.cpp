#include "spyro3_runtime.h"

#include "game.h"

#include <cstdlib>
#include <lucent/log.h>

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

void Spyro3Runtime::bootInit(Core &) {
  lucent::error("boot", "Spyro 3 is not the active conformance title");
  std::abort();
}

bool Spyro3Runtime::guestVramIsPicture(const Game &) const {
  return true;
}

} // namespace spyro3
