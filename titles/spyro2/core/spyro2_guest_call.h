#pragma once

#include <cstdint>
#include <string>
#include <string_view>

class Core;

namespace spyro2 {

// One finite guest call, resumed across product steps.
//
// A PSX guest call is not a host call: it legitimately outlives one host turn, it can stop at
// a display-field boundary the host owns, and it returns through an address captured BEFORE the
// first dispatch. This owns that whole contract once, so the frame driver's step loop reads as
// "advance the call, react to what it stopped for" instead of repeating the resume rules.
//
// The three ways it can stop are kept apart because they mean different things to the caller:
// `Returned` ends the call, `FieldBoundary` is an ordinary wait the host satisfies by
// delivering a field, and `BudgetExhausted` is the call still running when the turn ended. A
// fault is neither, and the caller must not treat it as progress.
class GuestCall {
public:
  enum class Outcome : std::uint8_t { Returned, FieldBoundary, BudgetExhausted, Fault };

  struct Result {
    Outcome outcome = Outcome::Returned;
    std::uint32_t guestPc = 0;
    std::string detail;
  };

  GuestCall(Core &core, std::string_view owner);

  // Enter the call at `entry`. The return address is the Core's current `$r[31]`, captured
  // before the first dispatch; every later resume reuses it, because a resume that adopted the
  // nested `$r[31]` the body left behind would end the call in the wrong place.
  void begin(std::uint32_t entry);

  [[nodiscard]] bool active() const {
    return active_;
  }

  [[nodiscard]] std::uint32_t entry() const {
    return entry_;
  }

  // Where the call will resume, or 0 when it is not active. A step's report names it so a run
  // that is making progress somewhere unreadable is still locatable.
  [[nodiscard]] std::uint32_t resumePc() const {
    return active_ ? resumePc_ : 0u;
  }

  [[nodiscard]] std::uint64_t turns() const {
    return turns_;
  }

  [[nodiscard]] std::uint64_t cycles() const {
    return cycles_;
  }

  // Run at most `maxTurns` host turns. Every turn is `ExecutionBudget::currentTurn`, so the
  // bound is in display fields whatever the caller was doing.
  Result advance(std::uint32_t maxTurns);

private:
  Core &core_;
  std::string owner_;
  std::uint32_t entry_ = 0;
  std::uint32_t resumePc_ = 0;
  std::uint32_t returnPc_ = 0;
  bool active_ = false;
  std::uint64_t turns_ = 0;
  std::uint64_t cycles_ = 0;
};

} // namespace spyro2
