#pragma once

#include "core.h"
#include "execution_control.h"
#include "guest_call.h"
#include "native_dispatch.h"

#include <concepts>
#include <cstdint>
#include <string_view>

namespace spyro {

// Register discipline for this title's overrides. The resume contract is
// `psx::cpu::ResumableGuestCall`; override keying and installation are `installNativeOverride` and
// friends, which resolve an address through the active image identity. What is left here is what
// only this title knows: what a `jal` leaves in `$ra`, and which registers a retail body expects
// back.

// A MIPS `jal` is four bytes and the address it leaves in `$ra` is the one after its delay slot, so
// every call site in an overridden body has one exact return address.
inline constexpr std::uint32_t kJalReturnOffset = 8u;

// `$ra` for a nested call is whatever the override last put there, never the guest's own, and a
// callee may keep it (`func_8004BE4C` spills `$ra` to a save area on entry at 0x8004BE80).
// `jalSite` is the address of the `jal` the retail body executes.
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

// A retail body returns with the `$ra` it entered with; the differential compares the register.
// Declare this first in an override.
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

// An override has no frame of its own, so a callee reading its arguments from `$sp` must see the
// retail body's lowered pointer for the length of the scope; the entry `$sp` is restored after,
// because the differential compares `$sp` at the return.
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

} // namespace spyro
