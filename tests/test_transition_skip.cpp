#include "spyro1_transition_skip.h"

#include <cstdlib>
#include <iostream>

namespace {

using spyro1::Cancellation;
using spyro1::classify;
using spyro1::flybyCardUp;
using spyro1::TransitionState;

constexpr std::uint32_t kPlaying = 0u;
constexpr std::uint32_t kLevelTransition = 1u;
constexpr std::uint32_t kPauseMenu = 2u;
constexpr std::uint32_t kExitLevel = 10u;
constexpr std::uint32_t kTitleScreen = 13u;

// The flyby card: GS_TitleScreen in TSM_Demo/TSS_Active on the TSD_Level path, with its level
// fully loaded. Every field matters, so the tests below vary them one at a time from here.
TransitionState flyby() {
  return {.stage = kTitleScreen,
          .titleMode = 3u,
          .titleState = 2u,
          .demoType = 1u,
          .loadStage = 13u,
          .skipPressed = true};
}

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

// The whole point of the card is that the player may press Start long before the level has
// streamed. classify must refuse then -- the guest's own terminal is gated on g_LoadStage 13 --
// while flybyCardUp must still say the card is up, because that is what makes TransitionSkip hold
// the press instead of dropping it.
void testHonoursTheFlybysLoadGate() {
  TransitionState loading = flyby();
  loading.loadStage = 6u;
  require(classify(loading) == Cancellation::None,
          "a press during the flyby's load does not cancel it early");
  require(flybyCardUp(loading),
          "the flyby card is still up while its level loads, so the press is held");
}

void testCancelsTheLoadedFlyby() {
  require(classify(flyby()) == Cancellation::CutsceneTransitionFlyby,
          "a Start press on the loaded flyby cancels it");
}

void testTheFlybyAlsoRequiresAPress() {
  TransitionState unpressed = flyby();
  unpressed.skipPressed = false;
  require(classify(unpressed) == Cancellation::None,
          "the flyby runs to its natural end without a press");
}

// TSD_Cutscene ends by starting cutscene playback and TSD_DemoLevel writes two demo globals this
// port has not recovered. Neither terminal is reproduced, so neither screen may be cancelled: a
// press there has to fall through rather than run the level path's terminal on the wrong screen.
void testLeavesTheUnrecoveredFlybyPathsAlone() {
  for (std::uint32_t demoType : {0u, 2u}) {
    TransitionState other = flyby();
    other.demoType = demoType;
    require(classify(other) == Cancellation::None,
            "a flyby path whose terminal is not recovered is not cancelled");
    require(!flybyCardUp(other), "an unrecovered flyby path does not hold the press either");
  }
}

// The titlescreen menu is the same gamestate. Start there is the menu's own confirm button, and
// TitlescreenUpdate rather than GamestateCutsceneTransition owns it.
void testLeavesTheTitlescreenMenuAlone() {
  TransitionState menu = flyby();
  menu.titleMode = 1u; // TSM_Menu
  require(classify(menu) == Cancellation::None, "titlescreen-menu Start is not a transition skip");
  TransitionState setup = flyby();
  setup.titleState = 0u; // TSS_Setup, before the card is on screen
  require(classify(setup) == Cancellation::None, "a press before the flyby is drawn is not a skip");
}

} // namespace

int main() {
  testCancelsTheDisplayedTally();
  testIgnoresAnEndedTally();
  testLeavesGameplayAndMenuInputAlone();
  testRequiresAPress();
  testCancelsTheReturnHomeGlideWhateverTheTallyFlagSays();
  testTheReturnHomeGlideAlsoRequiresAPress();
  testHonoursTheFlybysLoadGate();
  testCancelsTheLoadedFlyby();
  testTheFlybyAlsoRequiresAPress();
  testLeavesTheUnrecoveredFlybyPathsAlone();
  testLeavesTheTitlescreenMenuAlone();
  std::cout << "transition_skip: PASS (each cancellation is scoped to its own displayed screen)\n";
  return 0;
}
