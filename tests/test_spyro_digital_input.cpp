#include "core.h"
#include "game.h"
#include "lightrec_executor.h"
#include "native_dispatch.h"
#include "psx_exe_image.h"
#include "title_runtime_registry.h"
#include "title_selection.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <memory>

namespace {

constexpr std::uint32_t kMovement = 0x8003D3B8u;
constexpr std::uint32_t kActivePadPointer = 0x800757E0u;
constexpr std::uint32_t kBufferedPad = 0x800773BCu;
constexpr std::uint32_t kCameraRotation = 0x80076E20u;
constexpr std::uint32_t kBodyRotation = 0x80078A66u;
constexpr std::uint32_t kTargetSpeed = 0x80078B20u;
constexpr std::uint32_t kTargetRotation = 0x80078B24u;
constexpr std::uint32_t kTurnMomentum = 0x80078BA0u;
constexpr std::uint32_t kDirectionTable = 0x8006C5D0u;
constexpr std::uint32_t kLeft = 0x8000u;
constexpr std::uint32_t kSpeed = 0x600u;
constexpr std::uint32_t kCaller = 0x800F0000u;
constexpr std::int16_t kCameraAngle = -32;
constexpr std::uint8_t kBodyAngle = 0x25u;

void require(bool condition, const char *message) {
  if (!condition) {
    lucent::error("digital-input", "{}", message);
    std::exit(1);
  }
}

struct InputFrame {
  const char *name;
  std::uint32_t held;
  std::uint32_t pressed;
  std::uint32_t released;
};

void checkFrame(Core &core, const InputFrame &frame) {
  // Inputs are synthetic state, while the function and direction table remain authenticated retail
  // runtime data. The buffered layout differs from the outer Gamepad struct in the decomp
  // reference.
  core.mem_w32(kBufferedPad, 2u);
  core.mem_w32(kBufferedPad + 4u, frame.held);
  core.mem_w32(kBufferedPad + 8u, frame.pressed);
  core.mem_w32(kBufferedPad + 12u, frame.released);
  core.mem_w32(kBufferedPad + 16u, 0u);
  core.mem_w32(kBufferedPad + 20u, 0x7F7F7F7Fu);
  core.mem_w32(kTargetSpeed, 0xDEADu);
  core.mem_w32(kTargetRotation, 0xDEADu);
  core.mem_w32(kTurnMomentum, 0x1234u);
  core.r[4] = kSpeed;
  core.r[16] = 0x11111111u;
  core.r[17] = 0x22222222u;
  core.r[31] = kCaller;
  const auto stack = core.r[29];
  const auto before = core.lightrecExecutor().counters();
  const auto result =
      psx::cpu::dispatchGuest(core, kMovement, psx::cpu::ExecutionBudget::fromCycles(4096u));
  require(result.returned() && result.guestPc == kCaller, "retail movement did not return");
  require(core.r[29] == stack && core.r[31] == kCaller && core.r[16] == 0x11111111u &&
              core.r[17] == 0x22222222u,
          "retail movement violated its preserved-register/stack ABI");

  const auto direction = static_cast<std::int16_t>(core.mem_r16(kDirectionTable + 8u * 2u));
  const bool moving = frame.held != 0u;
  const auto expectedRotation = moving
                                    ? static_cast<std::uint32_t>(direction + kCameraAngle) & 0xFFFu
                                    : static_cast<std::uint32_t>(kBodyAngle) << 4u;
  require(core.mem_r32(kTargetSpeed) == (moving ? kSpeed : 0u),
          "movement must consume held +4, never released +12");
  require(core.mem_r32(kTargetRotation) == expectedRotation,
          "retail direction/camera or neutral body rotation changed");
  require(core.mem_r32(kTurnMomentum) == (moving ? 0x1234u : 0u),
          "neutral input must reset turn momentum; held input must preserve it");
  const auto after = core.lightrecExecutor().counters();
  const auto blocks = after.executedBlocks - before.executedBlocks;
  const auto instructions = after.executedInstructions - before.executedInstructions;
  require(blocks != 0u && instructions != 0u && after.fallback.calls == 0u &&
              after.fallback.instructions == 0u,
          "retail discriminator requires JIT blocks/instructions with zero fallback");
  lucent::info("digital-input",
               "{}: held=0x{:04X} down=0x{:04X} released=0x{:04X} speed={} rotation={} "
               "JIT blocks={} instructions={} fallback=0/{}",
               frame.name,
               frame.held,
               frame.pressed,
               frame.released,
               core.mem_r32(kTargetSpeed),
               core.mem_r32(kTargetRotation),
               blocks,
               instructions,
               instructions);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    lucent::error("digital-input",
                  "Usage: {} <path/to/SCUS_942.28>; local authenticated game file required; "
                  "scanned 0 input frames",
                  argv[0]);
    return 2;
  }
  const auto selected = spyro::selectExecutableFile(argv[1], spyro::executableCatalog());
  if (!selected || selected.identity->title != spyro::SpyroTitle::Spyro1) {
    lucent::error(
        "digital-input", "Spyro 1 executable refused: {}; scanned 0 input frames", selected.detail);
    return 2;
  }
  auto game = std::make_unique<Game>();
  Core &core = game->core;
  load_exe(argv[1], &core);
  core.mem_w32(kActivePadPointer, kBufferedPad);
  core.mem_w16(kCameraRotation, static_cast<std::uint16_t>(kCameraAngle));
  core.mem_w8(kBodyRotation, kBodyAngle);
  require(psx::cpu::classifyGuestHostDispatch(core, kMovement) ==
              psx::cpu::GuestHostDispatchKind::ExecuteGuest,
          "digital movement must execute the authenticated retail body");
  constexpr std::array frames{
      InputFrame{"press", kLeft, kLeft, 0u},
      InputFrame{"hold", kLeft, 0u, 0u},
      InputFrame{"release", 0u, 0u, kLeft},
      InputFrame{"idle", 0u, 0u, 0u},
  };
  for (const auto &frame : frames) {
    checkFrame(core, frame);
  }
  lucent::info("digital-input",
               "PASS: 4/4 authenticated retail input frames; no native movement override");
}
