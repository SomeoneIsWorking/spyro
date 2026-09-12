#include "spyro1_runtime.h"

#include "cd_control.h"
#include "cfg.h"
#include "fps60.h"
#include "frame_pacer.h"
#include "game.h"
#include "presentation_owner.h"
#include "spyro1_frame_driver.h"
#include "spyro_context.h"
#include "spyro_game.h"
#include "spyro_gate_debug.h"
#include "temporal_scene.h"
#include "temporal_scene_source.h"

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
  return new SpyroContext();
}

void Spyro1Runtime::destroyContext(void *context) {
  delete static_cast<SpyroContext *>(context);
}

void Spyro1Runtime::registerOverrides(Game &game) {
  spyro_register_cd_queue(game.core);
  spyro_register_native_rand(game.core);
  spyro_register_native_leaves(game.core);
  spyro_register_native_vec(game.core);
  spyro_register_native_gte(game.core);
  spyro_register_native_angle(game.core);
  spyro_register_native_util(game.core);
  lucent::info("boot", "installed Spyro 1's verified image-scoped native overrides");
}

void Spyro1Runtime::bootInit(Core &core) {
  frameDriver(core).initialize(core);
}

std::unique_ptr<FrameDriver> Spyro1Runtime::createFrameDriver(Game &game) {
  return std::make_unique<Spyro1FrameDriver>(game, cfg_dbg("stage-observe") != 0);
}

bool Spyro1Runtime::guestVramIsPicture(const Game &game) const {
  return spyro_presentation_owner(game.core).guestVramIsPicture();
}

void Spyro1Runtime::pacePresentation(Core &core, int fields, int parts) {
  // FieldScheduler has already delivered this simulated time, including the guest IRQ callbacks.
  gpu_wait_presented_fields(&core, fields, parts);
}

std::unique_ptr<TemporalFramePresentation>
Spyro1Runtime::createTemporalFramePresentation(Game &game) {
  return std::make_unique<Fps60>(game, spyro_temporal_scene_source(game));
}

namespace {

constexpr std::uint32_t kGpuTimeoutDeadlineVar = 0x80074B7Cu;
constexpr std::uint32_t kGpuTimeoutFlagVar = 0x80074B80u;
constexpr std::uint32_t kGpuTimeoutArm = 0x80062090u;
constexpr std::uint32_t kGpuTimeoutCheck = 0x800620C4u;
// The three libcd control entries, read from external/spyro-1's own labels in asm/psyq.s. They are
// NOT interchangeable: CdControl and CdControlB take (com, param, result), but CdControlF takes
// only (com, param), so at its call sites a2 is the caller's leftover register and must never be
// written through. This binding named 0x80063D80 CdControlB and gave it the result-writing owner,
// which wrote 8 bytes at whatever a2 held — 0x09B30000 on the level load after a portal entry.
constexpr std::uint32_t kCdControl = 0x80063C48u;
constexpr std::uint32_t kCdControlF = 0x80063D80u;
constexpr std::uint32_t kCdControlB = 0x80063EACu;
constexpr std::uint32_t kCdSync = 0x800647A0u;
constexpr std::uint32_t kCdCw = 0x80064CECu;
constexpr std::uint32_t kCdDataSync = 0x800655A0u;
constexpr std::uint32_t kCdInitHandshake = 0x800653B4u;

void gpuTimeoutArm(Core *core) {
  core->mem_w32(kGpuTimeoutDeadlineVar, 0x7fffffffu);
  core->mem_w32(kGpuTimeoutFlagVar, 0);
}

void syncComplete(Core *core) {
  core->r[2] = 0;
}

} // namespace

const PlatformHlePlan *Spyro1Runtime::platformHlePlan() const {
  static const PlatformHlePlan plan = [] {
    PlatformHlePlan p{};
    p.vsyncAddress = 0x8005DBC4u;
    p.setGeomOffset = 0x80062618u;
    p.setGeomScreen = 0x80062638u;
    p.drawSyncAddress = 0x8005F764u;
    p.bindings[0] = {kGpuTimeoutArm, gpuTimeoutArm};
    p.bindings[1] = {kGpuTimeoutCheck, syncComplete};
    p.bindings[2] = {kCdDataSync, syncComplete};
    p.bindings[3] = {kCdInitHandshake, syncComplete};
    p.bindings[4] = {kCdControl, cd_control_sync};
    p.bindings[5] = {kCdControlF, cd_control_fire_sync};
    p.bindings[6] = {kCdControlB, cd_control_sync};
    p.bindings[7] = {kCdSync, cd_sync_stock_sync};
    p.bindings[8] = {kCdCw, cd_command_stock_sync};
    p.bindingCount = 9;
    p.windowLo[0] = 0x8005B000u;
    p.windowHi[0] = 0x80066000u;
    return p;
  }();
  return &plan;
}

const GuestPadBufferLayout *Spyro1Runtime::guestPadBufferLayout() const {
  static constexpr GuestPadBufferLayout layout{
      .slot0Buffer = 0x800786A0u,
      .slot1Buffer = 0x800786C2u,
      .slotPointerTable = 0u,
      .slotPointerStride = 4u,
  };
  return &layout;
}

bool Spyro1Runtime::replCommand(Core &core, const char *command, const char *line) {
  return spyro::gate_debug::replCommand(&core, command, line);
}

} // namespace spyro1
