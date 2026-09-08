#pragma once

#include <cstdint>

class Core;

namespace spyro1 {

class FieldScheduler;

// The observable state a transition screen's cancellation decision is made from.
struct TransitionState {
  std::uint32_t stage = 0;
  std::uint32_t levelTransHudActive = 0;
  bool skipPressed = false;
};

enum class Cancellation : std::uint8_t {
  None,
  LevelTransitionTally,
};

// Decide which transition screen, if any, the current state authorises cancelling. Pure so the
// decision can be exercised without a running guest; the writes it authorises live in
// TransitionSkip::observe.
Cancellation classify(const TransitionState &state);

// Cancels a presentation-only transition screen when the player presses Start or Cross.
//
// A cancellation here is never a fast-forward: it performs exactly the terminal write the screen's
// own guest owner performs when that screen ends naturally, and leaves every other global — load
// stage, timers, sprite state — as the natural route leaves it. A screen whose terminal transition
// has not been recovered is deliberately absent rather than approximated, so no press can jump a
// state or bypass required I/O.
class TransitionSkip {
public:
  explicit TransitionSkip(FieldScheduler &fields);

  // Called once per logic frame, before the guest frame update reads the transition globals.
  void observe(Core &core);

  std::uint32_t cancellations() const {
    return cancellations_;
  }

private:
  FieldScheduler &fields_;
  std::uint32_t cancellations_ = 0;
};

} // namespace spyro1
