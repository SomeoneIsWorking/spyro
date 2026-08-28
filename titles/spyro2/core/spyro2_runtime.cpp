#include "spyro2_runtime.h"
#include "cd_control.h"
#include "core.h"
#include "platform_hle.h"
#include "spyro2_frame_driver.h"

#include <memory>

namespace spyro2 {
namespace {

constexpr std::uint32_t kCdRead = 0x80058108u;
constexpr std::uint32_t kCdInit = 0x800582B8u;
constexpr std::uint32_t kCdSync = 0x80058810u;
constexpr std::uint32_t kCdControl = 0x80058858u;
constexpr std::uint32_t kSetGeomScreen = 0x80057AE8u;
constexpr std::uint32_t kSetGeomOffset = 0x80057AF8u;
constexpr std::uint32_t kCdReadyCallbackPointer = 0x800663B8u;

// 0x800582B8 retries the retained controller handshake five times, but its only success-path state
// visible to callers is this callback table. The direct runtime owns every command and finite read
// synchronously, so no controller IRQ consumes the rest of libcd's private handshake state.
void cdInitSuccess(Core *core) {
  core->mem_w32(kCdReadyCallbackPointer, 0x800583D4u);
  core->mem_w32(0x800663BCu, 0u);
  core->mem_w32(0x80066700u, 0x80058384u);
  core->mem_w32(0x80066704u, 0x800583ACu);
  core->r[2] = 1u;
}

// Spyro 2's loader waits on its ready callback after CdRead. The framework transfers the complete
// finite read synchronously; deliver the matching CdlComplete callback once data is resident so the
// retained 0x80013810 state machine observes its normal termination condition without entering the
// stock CdRead VSync(-1) timeout path.
void cdReadWithCompletion(Core *core) {
  cd_read_stock_sync(core);
  if (core->r[2] == 0u) {
    return;
  }

  const std::uint32_t callback = core->mem_r32(kCdReadyCallbackPointer);
  if (callback == 0u) {
    return;
  }
  const R3000 saved = *static_cast<R3000 *>(core);
  core->r[4] = 2u;
  core->r[5] = 0u;
  rec_dispatch(core, callback);
  *static_cast<R3000 *>(core) = saved;
}

} // namespace

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

const PlatformHlePlan Spyro2Runtime::platformHlePlan_{
    .setGeomOffset = kSetGeomOffset,
    .setGeomScreen = kSetGeomScreen,
    .cdReadAddress = kCdRead,
    .vsyncAddress = 0x80058EDCu,
    .bindings = {{kCdRead, cdReadWithCompletion},
                 {kCdInit, cdInitSuccess},
                 {kCdSync, cd_sync_stock_sync},
                 {kCdControl, cd_control_sync}},
    .bindingCount = 4,
    .windowLo = {0x80058EDCu, 0x80057AE8u, 0x80058108u, 0x80058810u},
    .windowHi = {0x80059054u, 0x80057BA0u, 0x80058348u, 0x80058994u},
};

Spyro2Runtime::Spyro2Runtime(SubstrateInstaller substrateInstaller)
    : SpyroRuntime(programImage_, spyro::SpyroTitle::Spyro2),
      substrateInstaller_(substrateInstaller) {}

bool Spyro2Runtime::installSubstrate() {
  if (substrateInstaller_ == nullptr) {
    return false;
  }
  substrateInstaller_();
  return true;
}

std::string_view Spyro2Runtime::substrateRefusal() const {
  return substrateInstaller_ == nullptr
             ? "SCUS_944.25 is selected by a binary without the Spyro 2 substrate; use the "
               "title-local spyro2_port product"
             : std::string_view{};
}

void *Spyro2Runtime::createContext(Core &) {
  return nullptr;
}

void Spyro2Runtime::destroyContext(void *) {}

void Spyro2Runtime::registerOverrides(Game &) {}

void Spyro2Runtime::bootInit(Core &core) {
  frameDriver(core).initialize(core);
}

std::unique_ptr<FrameDriver> Spyro2Runtime::createFrameDriver(Game &game) {
  return std::make_unique<Spyro2FrameDriver>(game);
}

const PlatformHlePlan *Spyro2Runtime::platformHlePlan() const {
  return &platformHlePlan_;
}

bool Spyro2Runtime::guestVramIsPicture(const Game &) const {
  // The measured display bootstrap defines and clears guest DRAWENV/DISPENV buffers before the
  // product reaches any native producer. Its three finite boot fields therefore present guest VRAM.
  return true;
}

} // namespace spyro2
