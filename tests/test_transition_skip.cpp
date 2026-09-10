#include "spyro1_transition_skip.h"

#include <cstdlib>
#include <iostream>

namespace {

using spyro1::Cancellation;
using spyro1::classify;

constexpr std::uint32_t kPlaying = 0u;
constexpr std::uint32_t kLevelTransition = 1u;
constexpr std::uint32_t kPauseMenu = 2u;
constexpr std::uint32_t kExitLevel = 10u;

void require(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "transition_skip: " << what << '\n';
    std::exit(1);
  }
}

void testCancelsTheDisplayedTally() {
  require(classify({.stage = kLevelTransition, .levelTransHudActive = 1u, .skipPressed = true}) ==
              Cancellation::LevelTransitionTally,
          "a Start press on the displayed tally cancels it");
}

void testIgnoresAnEndedTally() {
  require(classify({.stage = kLevelTransition, .levelTransHudActive = 0u, .skipPressed = true}) ==
              Cancellation::None,
          "a tally that already ended is not cancelled again");
}

// Start is the pause button during play and the confirm button in the pause menu. A skip owner that
// classified on the press alone would swallow or duplicate those, so both must read as no
// cancellation even while the press is held.
void testLeavesGameplayAndMenuInputAlone() {
  require(classify({.stage = kPlaying, .levelTransHudActive = 1u, .skipPressed = true}) ==
              Cancellation::None,
          "gameplay Start is not a transition skip");
  require(classify({.stage = kPauseMenu, .levelTransHudActive = 1u, .skipPressed = true}) ==
              Cancellation::None,
          "pause-menu Start is not a transition skip");
}

void testRequiresAPress() {
  require(classify({.stage = kLevelTransition, .levelTransHudActive = 1u, .skipPressed = false}) ==
              Cancellation::None,
          "the tally runs to its natural end without a press");
}

// The return-home glide owns the whole of stage 10, so unlike the tally it has no separate
// liveness flag. The flag must therefore not gate it: reading a stale g_LevelTransHudActive of 0
// and refusing the cancellation would make the glide unskippable on exactly the common path, since
// the previous transition's tally clears that flag on its way out.
void testCancelsTheReturnHomeGlideWhateverTheTallyFlagSays() {
  require(classify({.stage = kExitLevel, .levelTransHudActive = 0u, .skipPressed = true}) ==
              Cancellation::ReturnHomeSequence,
          "a Start press during the return-home glide cancels it with the tally flag clear");
  require(classify({.stage = kExitLevel, .levelTransHudActive = 1u, .skipPressed = true}) ==
              Cancellation::ReturnHomeSequence,
          "a Start press during the return-home glide cancels it with the tally flag set");
}

void testTheReturnHomeGlideAlsoRequiresAPress() {
  require(classify({.stage = kExitLevel, .levelTransHudActive = 0u, .skipPressed = false}) ==
              Cancellation::None,
          "the return-home glide runs to its natural end without a press");
}

} // namespace

int main() {
  testCancelsTheDisplayedTally();
  testIgnoresAnEndedTally();
  testLeavesGameplayAndMenuInputAlone();
  testRequiresAPress();
  testCancelsTheReturnHomeGlideWhateverTheTallyFlagSays();
  testTheReturnHomeGlideAlsoRequiresAPress();
  std::cout << "transition_skip: PASS (each cancellation is scoped to its own displayed screen)\n";
  return 0;
}
