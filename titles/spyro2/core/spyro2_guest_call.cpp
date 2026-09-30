#include "spyro2_guest_call.h"

#include "core.h"
#include "execution_exit.h"
#include "guest_call.h"
#include "native_dispatch.h"

#include <lucent/log.h>

namespace spyro2 {
namespace {

constexpr std::uint32_t kGuestRamLow = 0x80000000u;
constexpr std::uint32_t kGuestRamHigh = 0x80200000u;

} // namespace

GuestCall::GuestCall(Core &core, std::string_view owner) : core_(core), owner_(owner) {}

void GuestCall::begin(std::uint32_t entry) {
  if (active_) {
    lucent::error("guest-call",
                  "{}: begin(0x{:08X}) while a call to 0x{:08X} is still active",
                  owner_,
                  entry,
                  entry_);
    return;
  }
  // A host-entered call has no guest caller to return to. `$r[31]` therefore carries whatever
  // the host last had there, and the executor stops the call when guest PC EQUALS it. A host
  // pointer cannot equal a KSEG0 guest PC, which is what makes the call finite; a `$r[31]`
  // that happens to sit inside guest RAM could stop the call in the middle of it, so that is
  // refused here rather than discovered as a mysterious early return.
  returnPc_ = core_.r[31];
  if (returnPc_ >= kGuestRamLow && returnPc_ < kGuestRamHigh) {
    lucent::error("guest-call",
                  "{}: refusing to enter 0x{:08X} with `$r[31]` = 0x{:08X} inside guest RAM; the "
                  "call would stop at an address it never reaches",
                  owner_,
                  entry,
                  returnPc_);
    return;
  }
  entry_ = entry;
  resumePc_ = entry;
  active_ = true;
  turns_ = 0;
  cycles_ = 0;
}

GuestCall::Result GuestCall::advance(std::uint32_t maxTurns) {
  Result result{};
  if (!active_) {
    result.outcome = Outcome::Returned;
    return result;
  }
  for (std::uint32_t turn = 0; turn < maxTurns;) {
    const auto execution =
        resumePc_ == entry_ && turns_ == 0
            ? psx::cpu::dispatchGuest(
                  core_, entry_, psx::cpu::ExecutionBudget::currentTurn(core_), owner_)
            : psx::cpu::resumeGuestToReturnFrom(core_,
                                                entry_,
                                                resumePc_,
                                                returnPc_,
                                                psx::cpu::ExecutionBudget::currentTurn(core_));
    ++turn;
    ++turns_;
    cycles_ += execution.cycles;
    result.guestPc = execution.guestPc;
    result.detail = execution.detail;
    switch (execution.reason) {
    case psx::cpu::ExecutionExitReason::GuestReturn:
      active_ = false;
      result.outcome = Outcome::Returned;
      return result;
    case psx::cpu::ExecutionExitReason::FrameBoundary:
      // A display wait the host owns. The resume point is the guest's own continuation, which
      // the framework's leaf dispatch already set, so nothing here has to invent one.
      resumePc_ = execution.guestPc;
      result.outcome = Outcome::FieldBoundary;
      return result;
    case psx::cpu::ExecutionExitReason::BudgetExhausted:
      resumePc_ = execution.guestPc;
      result.outcome = Outcome::BudgetExhausted;
      return result;
    default:
      active_ = false;
      result.outcome = Outcome::Fault;
      return result;
    }
  }
  result.outcome = Outcome::BudgetExhausted;
  return result;
}

} // namespace spyro2
