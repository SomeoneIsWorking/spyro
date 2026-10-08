#include "native_level_initialization.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>
#include <string_view>

namespace spyro1::native {
namespace {

// Every guest global below is a lui plus an immediate the body at 0x8001277C itself executes, so
// an address audit decodes each one and refuses the module if any of them is a
// hand-typed address the retail code never builds.
constexpr std::uint32_t kLevelId = 0x8007596Cu;
constexpr std::uint32_t kActEnabled = 0x800757A4u;
constexpr std::uint32_t kCameraMode = 0x80075914u;

// The guest callee the body calls, named by the `jal` at the call site the comment gives. A callee
// is the 26-bit field of that instruction rather than a `lui`+immediate pair, and
// an address audit re-derives it from the `jal` itself.
constexpr std::uint32_t kResetSavedState = 0x80012604u; // `jal` at 0x800127A8

constexpr std::uint32_t kArtisansLevelId = 0xAu;
constexpr std::uint32_t kActuatorsOn = 1u;
constexpr std::uint32_t kPassiveCameraMode = 0x52u;

constexpr std::string_view kOverrideName = "initialise_artisans_level";

// ── 0x8001277C — the Artisan's-level entry: mark level 10 resident, arm the actuators and put the
//     camera in passive mode, then hand off to the saved-state reset. The three stores go through
//     the same `lui $at, 0x8007` base the body reloads before each one, so each address is that
//     base plus its own positive immediate. v0 exits holding the camera mode unless the reset
//     returns a value of its own, because the body's last write before the call is the `addiu
//     $v0, $zero, 0x52` the camera-mode store consumes; a0-a3 are passed on as the caller left
//     them because the body sets up no arguments for the reset.
void initializeArtisansLevel(Core *c) {
  c->mem_w32(kLevelId, kArtisansLevelId);
  c->mem_w32(kActEnabled, kActuatorsOn);
  c->mem_w32(kCameraMode, kPassiveCameraMode);
  c->r[2] = kPassiveCameraMode;
  psx::cpu::callGuestNow(*c, kOverrideName, kResetSavedState, c->r[4], c->r[5], c->r[6], c->r[7]);
}

} // namespace

void registerLevelInitializationOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x8001277Cu, "initialise_artisans_level", initializeArtisansLevel);
}

} // namespace spyro1::native
