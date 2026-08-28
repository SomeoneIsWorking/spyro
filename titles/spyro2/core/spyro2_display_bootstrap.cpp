#include "spyro2_display_bootstrap.h"

#include "core.h"
#include "game.h"
#include "guest_call.h"
#include "spyro2_gpu_sync.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro2 {
namespace {

constexpr std::uint32_t kDisplayFrameBytes = 40u;
constexpr std::uint32_t kClearFrameBytes = 32u;

constexpr std::uint32_t kSetDispMask = 0x8005574Cu;
constexpr std::uint32_t kResetGraph = 0x8005557Cu;
constexpr std::uint32_t kSetGraphDebug = 0x800556F0u;
constexpr std::uint32_t kSetDefDispEnv = 0x80058E1Cu;
constexpr std::uint32_t kSetDefDrawEnv = 0x800590ECu;
constexpr std::uint32_t kClearImage = 0x80055968u;
constexpr std::uint32_t kPutDispEnv = 0x80055CA0u;
constexpr std::uint32_t kPutDrawEnv = 0x80055BE0u;

constexpr std::uint32_t kDisplayEnv0 = 0x80069928u;
constexpr std::uint32_t kDisplayEnv1 = 0x8006999Cu;
constexpr std::uint32_t kDrawEnv0 = 0x80069984u;
constexpr std::uint32_t kDrawEnv1 = 0x800699F8u;
constexpr std::uint32_t kCurrentDisplayEnv = 0x80066FC0u;

constexpr std::uint32_t kAfterFirstWait = 0x80011BD8u;
constexpr std::uint32_t kAfterNestedWait = 0x8004C49Cu;
constexpr std::uint32_t kAfterFinalWait = 0x80011CE0u;

void call(Core &core, std::uint32_t address, std::uint32_t returnAddress) {
  core.r[31] = returnAddress;
  rc0(&core, address);
}

void setDefEnv(Core &core,
               std::uint32_t function,
               std::uint32_t env,
               std::uint32_t y,
               std::uint32_t height,
               std::uint32_t returnAddress) {
  core.r[4] = env;
  core.r[5] = 0u;
  core.r[6] = y;
  core.r[7] = 512u;
  core.mem_w32(core.r[29] + 16u, height);
  call(core, function, returnAddress);
}

} // namespace

Spyro2DisplayBootstrap::Spyro2DisplayBootstrap(Game &game) : game_(game) {}

void Spyro2DisplayBootstrap::begin(Core &core) {
  if (begun_) {
    lucent::error("spyro2-display", "Spyro 2 display bootstrap entered twice");
    std::abort();
  }
  begun_ = true;

  // Retain the exact 0x80011BBC frame across its three measured field waits. The generated body is
  // deliberately kept in the substrate for A/B, but is never dispatched by the product owner.
  core.r[29] -= kDisplayFrameBytes;
  core.mem_w32(core.r[29] + 36u, core.r[31]);
  core.mem_w32(core.r[29] + 32u, core.r[18]);
  core.mem_w32(core.r[29] + 28u, core.r[17]);
  core.mem_w32(core.r[29] + 24u, core.r[16]);
  core.r[4] = 0u;
  core.r[31] = kAfterFirstWait;
}

void Spyro2DisplayBootstrap::deliverField(Core &core) {
  // Each former VSync(0) boundary is one finite host field and one framework presentation fence.
  game_.presentation.commit(&core, 1);
}

void Spyro2DisplayBootstrap::configureDisplay(Core &core) {
  core.r[4] = 0u;
  call(core, kSetDispMask, 0x80011BE0u);
  core.r[4] = 0u;
  call(core, kResetGraph, 0x80011BE8u);
  core.r[4] = 0u;
  call(core, kSetGraphDebug, 0x80011BF0u);

  core.r[16] = kDisplayEnv0;
  core.r[17] = 216u;
  setDefEnv(core, kSetDefDispEnv, kDisplayEnv0, 12u, 216u, 0x80011C14u);
  core.r[18] = kDisplayEnv1;
  setDefEnv(core, kSetDefDispEnv, kDisplayEnv1, 240u, 216u, 0x80011C30u);
  core.r[17] = 240u;
  setDefEnv(core, kSetDefDrawEnv, kDrawEnv0, 228u, 240u, 0x80011C4Cu);
  core.r[16] = kDrawEnv1;
  setDefEnv(core, kSetDefDrawEnv, kDrawEnv1, 0u, 240u, 0x80011C68u);

  core.mem_w16(0x800699A6u, 228u);
  core.mem_w16(0x80069930u, 0u);
  core.mem_w16(0x80069932u, 0u);
  core.mem_w16(0x800699A4u, 0u);
  core.mem_w16(0x80069A00u, 0u);
  core.mem_w16(0x8006998Cu, 0u);
  core.mem_w16(0x80069A02u, 0u);
  core.mem_w16(0x8006998Eu, 0u);
  core.mem_w8(0x80069940u, 1u);
  core.mem_w8(0x800699B4u, 1u);
  core.mem_w8(0x8006993Eu, 1u);
  core.mem_w8(0x800699B2u, 1u);
}

void Spyro2DisplayBootstrap::beginClear(Core &core) {
  // 0x8004C484 has its own retained frame. Split it at its nested VSync instead of dispatching a
  // body which could reach the mandatory fatal guest trap.
  core.r[29] -= kClearFrameBytes;
  core.mem_w32(core.r[29] + 24u, 0x80011CD8u);
  core.r[4] = 0u;
  core.r[31] = 0x8004C494u;
  completeDrawSync(core);
  core.r[4] = 0u;
  core.r[31] = kAfterNestedWait;
}

void Spyro2DisplayBootstrap::clearDisplay(Core &core) {
  const std::uint32_t rect = core.r[29] + 16u;
  core.mem_w16(rect, 0u);
  core.mem_w16(rect + 2u, 0u);
  core.mem_w16(rect + 4u, 512u);
  core.mem_w16(rect + 6u, 240u);
  core.r[4] = rect;
  core.r[5] = 0u;
  core.r[6] = 0u;
  core.r[7] = 0u;
  call(core, kClearImage, 0x8004C4C8u);

  core.mem_w16(rect + 2u, 228u);
  core.r[4] = rect;
  core.r[5] = 0u;
  core.r[6] = 0u;
  core.r[7] = 0u;
  call(core, kClearImage, 0x8004C4E4u);
  core.r[4] = 0u;
  core.r[31] = 0x8004C4ECu;
  completeDrawSync(core);

  core.r[31] = core.mem_r32(core.r[29] + 24u);
  core.r[29] += kClearFrameBytes;
  core.r[4] = 0u;
  core.r[31] = kAfterFinalWait;
}

void Spyro2DisplayBootstrap::finishDisplay(Core &core) {
  core.mem_w32(kCurrentDisplayEnv, kDisplayEnv1);
  core.r[4] = kDrawEnv1;
  call(core, kPutDispEnv, 0x80011CF0u);
  core.r[4] = core.mem_r32(kCurrentDisplayEnv);
  call(core, kPutDrawEnv, 0x80011D00u);
  core.r[4] = 1u;
  call(core, kSetDispMask, 0x80011D08u);

  core.r[31] = core.mem_r32(core.r[29] + 36u);
  core.r[18] = core.mem_r32(core.r[29] + 32u);
  core.r[17] = core.mem_r32(core.r[29] + 28u);
  core.r[16] = core.mem_r32(core.r[29] + 24u);
  core.r[29] += kDisplayFrameBytes;
}

void Spyro2DisplayBootstrap::step(Core &core) {
  if (!begun_) {
    lucent::error("spyro2-display", "Spyro 2 display bootstrap stepped before entry");
    std::abort();
  }

  deliverField(core);
  switch (phase_) {
  case Phase::FirstField:
    configureDisplay(core);
    beginClear(core);
    phase_ = Phase::ClearField;
    return;
  case Phase::ClearField:
    clearDisplay(core);
    phase_ = Phase::FinalField;
    return;
  case Phase::FinalField:
    finishDisplay(core);
    phase_ = Phase::Complete;
    lucent::info("spyro2-display",
                 "owned display bootstrap 0x80011BBC across three host fields; reached boot-prefix "
                 "return 0x80011EB4 without dispatching guest VSync");
    return;
  case Phase::Complete:
    lucent::error("spyro2-display", "completed Spyro 2 display bootstrap stepped again");
    std::abort();
  }
  std::abort();
}

bool Spyro2DisplayBootstrap::complete() const {
  return phase_ == Phase::Complete;
}

} // namespace spyro2
