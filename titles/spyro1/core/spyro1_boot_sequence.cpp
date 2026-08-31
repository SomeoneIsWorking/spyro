#include "spyro1_boot_sequence.h"

#include "core.h"
#include "guest_call.h"
#include "spyro1_field_scheduler.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro1 {
namespace {

constexpr std::uint32_t kBootStackBytes = 72u;
constexpr std::uint32_t kRect24 = 24u;
constexpr std::uint32_t kRect32 = 32u;
constexpr std::uint32_t kLogoBytes = 0x5A000u;
constexpr std::uint32_t kLogoHoldFields = 0xD2u;

void call(Core &core,
          std::uint32_t address,
          std::uint32_t a0 = 0,
          std::uint32_t a1 = 0,
          std::uint32_t a2 = 0,
          std::uint32_t a3 = 0) {
  core.r[4] = a0;
  core.r[5] = a1;
  core.r[6] = a2;
  core.r[7] = a3;
  rc0(&core, address);
}

void writeRect(Core &core,
               std::uint32_t address,
               std::int16_t x,
               std::int16_t y,
               std::int16_t width,
               std::int16_t height) {
  core.mem_w16(address + 0u, static_cast<std::uint16_t>(x));
  core.mem_w16(address + 2u, static_cast<std::uint16_t>(y));
  core.mem_w16(address + 4u, static_cast<std::uint16_t>(width));
  core.mem_w16(address + 6u, static_cast<std::uint16_t>(height));
}

} // namespace

BootSequence::BootSequence(FieldScheduler &fields) : fields_(fields) {}

void BootSequence::initialize(Core &core) {
  if (initialized_) {
    lucent::error("boot-native", "Spyro 1 boot sequence initialized twice");
    std::abort();
  }
  initialized_ = true;
  originalStack_ = core.r[29];
  core.r[29] -= kBootStackBytes;
  fields_.bootSequenceBegin();

  call(core, 0x800122A8u);
  call(core, 0x80012460u);
  call(core, 0x800123C8u);
  call(core, 0x8005595Cu);
  call(core, 0x80012480u);
  call(core, 0x8001256Cu);
  call(core, 0x80062350u);
  call(core, 0x80062618u, 0x100u, 0x78u);
  call(core, 0x80062638u, 0x155u);
  core.mem_w32(0x800785D8u, 0x8007DDE8u);
  core.mem_w32(0x800785DCu, 0x8007DDE8u);
  firstHoldStart_ = static_cast<std::uint32_t>(fields_.counter());
  call(core, 0x8005F6C8u, 0u);
  core.mem_w8(0x80076F4Du, 1u);
  core.mem_w8(0x80076FD1u, 1u);
  call(core, 0x80016914u, core.mem_r32(0x800785D8u), 0u, kLogoBytes);
  lucent::info(
      "boot-native", "native boot 0x800127C0/0x8001286C initialized at field {}", firstHoldStart_);
}

bool BootSequence::complete() const {
  return phase_ == Phase::Complete;
}

void BootSequence::deliverField(Core &core, const char *site) {
  if (!fields_.deliver(
          {.site = site, .present = true, .pace = true, .acknowledgeHostTurn = true})) {
    lucent::error("boot-native", "field scheduler refused boot field at {}", site);
    std::abort();
  }
}

void BootSequence::drawLogoField(Core &core,
                                 std::uint32_t source,
                                 std::uint32_t destination,
                                 int offset) {
  call(core, 0x80017F24u, source, destination, static_cast<std::uint32_t>(offset));
  const std::uint32_t rect = core.r[29] + kRect24;
  writeRect(core, rect, 0, 0, 0x300, 0xF0);
  call(core, 0x8005FA28u, rect, destination);
  call(core, 0x8005F764u, 0u);
  deliverField(core, "boot-logo");
  call(core, 0x80060030u, 0x80076FC0u);
}

void BootSequence::loadAssets(Core &core) {
  call(core, 0x8001250Cu);
  core.mem_w32(core.r[29] + 16u, 600u);
  call(core,
       0x80016500u,
       core.mem_r32(0x80076B90u),
       core.mem_r32(0x800113A0u),
       0x800u,
       core.mem_r32(0x8007A6E8u));
  call(core, 0x80016958u, 0x80076C00u, core.mem_r32(0x800113A0u), 0x1D0u);
  assetBase_ = 0x801C0000u - core.mem_r32(0x800755A4u);
  call(core,
       0x80016500u,
       core.mem_r32(0x80076B90u),
       assetBase_,
       0x40000u,
       core.mem_r32(0x80076C00u) + core.mem_r32(0x8007A6E8u));
  call(core,
       0x80016500u,
       core.mem_r32(0x80076B90u),
       core.mem_r32(0x800113A0u),
       core.mem_r32(0x8007A6E4u),
       core.mem_r32(0x8007A6E0u));
  logoSource_ = assetBase_ - core.mem_r32(0x8007A6D4u);
  call(core,
       0x80016500u,
       core.mem_r32(0x80076B90u),
       logoSource_,
       core.mem_r32(0x8007A6D4u),
       core.mem_r32(0x8007A6D0u));
  logoDestination_ = logoSource_ - kLogoBytes;
}

void BootSequence::finalize(Core &core) {
  call(core, 0x8005F6C8u, 0u);
  core.mem_w8(0x80076F4Du, 0u);
  core.mem_w8(0x80076FD1u, 0u);
  call(core, 0x80060030u, 0x80076FC0u);

  std::uint32_t rect = core.r[29] + kRect32;
  writeRect(core, rect, 0, 0, 0x200, 0x1E0);
  call(core, 0x8005F8F8u, rect, 0u, 0u, 0u);
  call(core, 0x8005F764u, 0u);
  call(core, 0x8005F6C8u, 1u);
  rect = core.r[29] + kRect24;
  writeRect(core, rect, 0x200, 0, 0x200, 0x100);
  call(core, 0x8005FA28u, rect, assetBase_);
  call(core, 0x8005F764u, 0u);
  core.mem_w32(0x800785FCu, 0x80200000u - core.mem_r32(0x800755A4u));
  call(core, 0x8005B7D8u);
  call(core, 0x8002D338u);
  core.mem_w32(0x80075918u, 0xFu);
  call(core, 0x8002D170u);
  core.mem_w32(0x800756CCu, 2u);
  core.mem_w32(0x80075760u, 0u);
  core.mem_w32(0x800785CCu, 0x8000u);
  fields_.bootSequenceEnd();
  core.r[29] = originalStack_;
  fields_.armHostClock();
  lucent::info("boot-native", "native boot reached the gameplay frame boundary");
}

bool BootSequence::step(Core &core) {
  if (!initialized_) {
    lucent::error("boot-native", "boot step called before initialization");
    std::abort();
  }

  // A FrameDriver step is one presented product frame. The retail boot has zero-field transition
  // work between its visible fields (asset loads and final setup); fold those transitions into the
  // adjacent step instead of returning them as fake frames or inventing an extra presentation.
  for (;;) {
    switch (phase_) {
    case Phase::FadeFirstIn:
      drawLogoField(core, 0x8006FCF4u, core.mem_r32(0x800785D8u), -0xE0 + iteration_ * 0x20);
      if (iteration_++ == 0) {
        call(core, 0x8005F6C8u, 1u);
      }
      if (iteration_ == 8) {
        iteration_ = 0;
        phase_ = Phase::LoadAssets;
      }
      return false;
    case Phase::LoadAssets:
      loadAssets(core);
      phase_ = Phase::HoldFirst;
      break;
    case Phase::HoldFirst:
      if (static_cast<std::uint32_t>(fields_.counter()) - firstHoldStart_ < kLogoHoldFields) {
        deliverField(core, "boot-first-hold");
        return false;
      }
      call(core, 0x80016914u, core.mem_r32(0x800785D8u), 0u, kLogoBytes);
      phase_ = Phase::FadeFirstOut;
      break;
    case Phase::FadeFirstOut:
      drawLogoField(core, 0x8006FCF4u, core.mem_r32(0x800785D8u), -(iteration_ + 1) * 0x20);
      if (++iteration_ == 8) {
        iteration_ = 0;
        secondHoldStart_ = static_cast<std::uint32_t>(fields_.counter());
        call(core, 0x80016914u, logoDestination_, 0u, kLogoBytes);
        phase_ = Phase::FadeSecondIn;
      }
      return false;
    case Phase::FadeSecondIn:
      drawLogoField(core, logoSource_, logoDestination_, -0xE0 + iteration_ * 0x20);
      if (++iteration_ == 8) {
        iteration_ = 0;
        core.mem_w32(0x80075864u, 3u);
        core.mem_w32(0x8007566Cu, 0u);
        phase_ = Phase::AdvanceLoadState;
      }
      return false;
    case Phase::AdvanceLoadState:
      call(core, 0x80014564u);
      if (core.mem_r32(0x80075864u) < 10u) {
        deliverField(core, "boot-load-state");
        return false;
      }
      phase_ = Phase::HoldSecond;
      break;
    case Phase::HoldSecond:
      if (static_cast<std::uint32_t>(fields_.counter()) - secondHoldStart_ < kLogoHoldFields) {
        deliverField(core, "boot-second-hold");
        return false;
      }
      phase_ = Phase::FadeSecondOut;
      break;
    case Phase::FadeSecondOut:
      drawLogoField(core, logoSource_, logoDestination_, -(iteration_ + 1) * 0x20);
      if (++iteration_ == 8) {
        phase_ = Phase::Finalize;
      }
      return false;
    case Phase::Finalize:
      finalize(core);
      phase_ = Phase::Complete;
      return true;
    case Phase::Complete:
      return true;
    }
  }
}

} // namespace spyro1
