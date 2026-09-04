#include "spyro2_runtime.h"

#include "game.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro2 {

const GuestProgramImage Spyro2Runtime::programImage_{
    .bss = {0x80066ED8u, 0x8006D264u},
    .stackTopWordAddress = 0x80066D3Cu,
    .stackReserveWordAddress = 0x80066D38u,
    .heapBase = 0x8006D264u,
    .heapSizeStoreAddress = 0x8006509Cu,
    .heapBaseStoreAddress = 0x80065098u,
    .globalPointer = 0x80066D38u,
    .libcInitEntry = 0x8005ABD8u,
    .gameMainEntry = 0x80011ADCu,
    .crt0Entry = 0x8005478Cu,
    .residentText = {0x00010000u, 0x00067000u},
    .backtraceText = {},
    .stackBias = {true, -8},
};

Spyro2Runtime::Spyro2Runtime() : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro2) {}

void *Spyro2Runtime::createContext(Core &) {
  return nullptr;
}

void Spyro2Runtime::destroyContext(void *) {}

void Spyro2Runtime::registerOverrides(Game &) {}

void Spyro2Runtime::bootInit(Core &) {
  lucent::error("boot", "Spyro 2 is not the active conformance title");
  std::abort();
}

bool Spyro2Runtime::guestVramIsPicture(const Game &) const {
  return true;
}

} // namespace spyro2
