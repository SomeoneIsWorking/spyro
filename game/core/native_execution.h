#pragma once

#include "core.h"
#include "execution_control.h"
#include "guest_call.h"
#include "native_dispatch.h"

#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <string_view>

namespace spyro {

// A MIPS `jal` is four bytes and the address it leaves in `$ra` is the one after its delay slot, so
// every call site in an overridden body has one exact return address.
inline constexpr std::uint32_t kJalReturnOffset = 8u;

// THE RETURN ADDRESS A NESTED GUEST CALL RUNS WITH. `dispatchGuest` hands `core.r[31]` to the
// executor as the callee's return address, so `$ra` for a nested call is whatever the override last
// put there — never the guest's own. An override calling `callGuestNow` directly therefore ran
// every callee with the OVERRIDE's caller address in `$ra`, which is not a value the retail body
// ever leaves behind, and the callee is free to keep it: `func_8004BE4C` spills `$ra` to a global
// save area on entry (`sw $ra, 0x2C($at)` at 0x8004BE80) that outlives the call.
//
// Measured 2026-10-01 on the attract route with `PSXPORT_OVERRIDE_DIFF_EVERY=1`
// (docs/issues/0150): `camera_collision_update` differed from retail in exactly two bytes, at
// 0x80077E04 — the `$ra` word `func_8004BE4C` last spilled — and `allocate_particle_slot` exited
// with `$ra` 0x8008A4BC where retail's `jal rand` at 0x80053598 left 0x800535A0. Both are this.
// `jalSite` is the address of the `jal` the retail body executes; tools/
// override_call_sites.py re-derives both it and the callee from the provisioned executable, so a
// literal that names the wrong call cannot pass the gate.
template <typename... Args>
  requires(sizeof...(Args) <= 4 && (std::convertible_to<Args, std::uint32_t> && ...))
void callGuestJumpedFrom(Core &core,
                         std::string_view owner,
                         std::uint32_t jalSite,
                         std::uint32_t address,
                         Args... args) {
  core.r[31] = jalSite + kJalReturnOffset;
  psx::cpu::callGuestNow(core, owner, address, args...);
}

// An override that reaches its callees through callGuestJumpedFrom leaves `$ra` holding its last
// `jal` address, but a retail body returns with the `$ra` it entered with (its epilogue reloads it
// from its frame, or it never changed it), and the differential compares the register. Declared
// first in an override, this puts the entry value back on every exit, including early returns.
class PreservedReturnAddress {
public:
  explicit PreservedReturnAddress(Core &core) : core_(core), entry_(core.r[31]) {}
  PreservedReturnAddress(const PreservedReturnAddress &) = delete;
  PreservedReturnAddress &operator=(const PreservedReturnAddress &) = delete;
  ~PreservedReturnAddress() {
    core_.r[31] = entry_;
  }

private:
  Core &core_;
  std::uint32_t entry_;
};

// A retail body lowers $sp for its own frame, and a callee that reads its arguments or frame from
// $sp, or spills below it, must see that lowered pointer — an override has no frame of its own, so
// the frame pointer is presented for the length of the scope and the entry $sp put back after,
// because the differential compares $sp at the return and both paths return with the caller's own.
class GuestFrameScope {
public:
  GuestFrameScope(Core &core, std::uint32_t frame) : core_(core), entry_(core.r[29]) {
    core_.r[29] = frame;
  }
  GuestFrameScope(const GuestFrameScope &) = delete;
  GuestFrameScope &operator=(const GuestFrameScope &) = delete;
  ~GuestFrameScope() {
    core_.r[29] = entry_;
  }

private:
  Core &core_;
  std::uint32_t entry_;
};

inline bool dispatchGuestOrPropagate(Core &core, std::uint32_t address) {
  return psx::cpu::completeOrPropagate(
      core, psx::cpu::dispatchGuest(core, address, psx::cpu::ExecutionBudget::currentTurn(core)));
}

inline bool callOriginalOrPropagate(Core &core, std::uint32_t address) {
  return psx::cpu::completeOrPropagate(
      core, psx::cpu::callOriginal(core, address, psx::cpu::ExecutionBudget::currentTurn(core)));
}

inline void installNativeOverride(Core &core,
                                  std::uint32_t address,
                                  std::string_view name,
                                  psx::cpu::NativeFunction function) {
  const auto image = core.currentImageIdentity(address);
  if (!image) {
    lucent::error("override",
                  "cannot install {} at 0x{:08X}: no active image owns that address",
                  name,
                  address);
    std::abort();
  }
  if (!core.nativeDispatcher().install({psx::cpu::NativeKey{*image, address}, name, function})) {
    lucent::error("override",
                  "cannot install {} at 0x{:08X}: that image/address already has an owner",
                  name,
                  address);
    std::abort();
  }
}

} // namespace spyro
