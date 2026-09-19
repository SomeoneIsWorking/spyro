#include "spyro1_transition_skip.h"
#include "guest_globals.h"

#include "core.h"
#include "guest_call.h"
#include "spyro1_field_scheduler.h"

#include <lucent/log.h>

namespace spyro1 {
namespace {

// g_Gamestate. Stage 1 is GS_LevelTransition, whose update is func_8002DF9C.
using spyro::guest::kGamestate;
constexpr std::uint32_t kStageLevelTransition = 1u;

// GS_ExitLevel, whose update is func_8002E084: Spyro glides out of the level while two counters
// run, and on their second wrap the sequence calls func_8002C664 and hands the screen to the level
// transition. That call IS the sequence's whole terminal route — it resets the specular table,
// picks the level's homeworld as the next level, rewinds the load stage and transition ticks, and
// sets the gamestate and its three flags — so dispatching it is the recovered cancellation rather
// than a transcription of it. Its ten globals are deliberately NOT copied here: a hand-written
// second copy of a guest terminal transition is exactly the thing that drifts.
constexpr std::uint32_t kStageExitLevel = 10u;
constexpr std::uint32_t kReturnHome = 0x8002C664u;
// Enough for one leaf transition that calls SpecularReset and writes ten globals; a budget this
// call cannot exhaust is a budget that cannot report the call going wrong.
constexpr std::uint64_t kReturnHomeCycles = 2u << 20;

// g_LevelTransHudActive. func_8002DA74 owns the tally screen and, once g_LevelTransTicks passes
// 416, clears this flag and nothing else: the gem sprites it left mid-flight keep their live
// markers, and g_LevelTransTicks itself is only ever re-zeroed when the next transition begins
// (gamestates/init.c func_8002C664 and special_surfaces.c). Clearing the flag is therefore the
// whole terminal transition, not a shortcut past part of it.
constexpr std::uint32_t kLevelTransHudActive = 0x800756B0u;

// g_LoadStage, reported so a cancellation can be read against the load it did not touch, and the
// flyby's own load gate.
using spyro::guest::kLoadStage;

// GS_TitleScreen. gamestates/update.c dispatches GamestateCutsceneTransition -- the flyby card the
// game shows on its way into a level -- from this stage when m_Mode is TSM_Demo; TitlescreenUpdate
// owns every other mode, so the mode is part of identifying the screen rather than a refinement.
using spyro::guest::kTitlescreenState;
constexpr std::uint32_t kStageTitleScreen = 13u;
constexpr std::uint32_t kTitleModeDemo = 3u;    // TSM_Demo
constexpr std::uint32_t kTitleStateActive = 2u; // TSS_Active, the flyby animation itself
constexpr std::uint32_t kDemoTypeLevel = 1u;    // TSD_Level

// titlescreen.h, four-byte fields in declaration order.
constexpr std::uint32_t kTitleMode = kTitlescreenState + 0x00u;
constexpr std::uint32_t kTitleState = kTitlescreenState + 0x04u;
constexpr std::uint32_t kTitleDemoType = kTitlescreenState + 0x1Cu;

// The level path's terminal transition, recovered from SCUS_942.28 rather than named by the decomp:
// 0x80033158 is `jal 0x8004AC24` and 0x80033160, eight bytes later, is `jal 0x80015370`, which is
// exactly the `func_8004AC24(1); LoadLevel(1); return;` the decompiled TSS_Active branch ends on.
// 0x80015370 is LoadLevel on three independent checks -- it is the only one of func_8002DF9C's jal
// targets that both reads and writes g_LoadStage (twenty accesses, three of them sw), one of its
// eight call sites is 0x8002DFE8 inside func_8002DF9C, and it appears in this pair. See
// docs/issues/0129.
//
// Both are dispatched rather than hand-copied, and the card the player sees here is TSD_Level,
// whose terminal is these two calls and nothing else: the two demo globals in the decompiled branch
// belong to TSD_DemoLevel, and the TSD_Cutscene path ends somewhere else entirely. Those two
// screens are therefore deliberately NOT cancellable -- their terminals are not recovered, and
// approximating one is the fast-forward this class exists to avoid.
constexpr std::uint32_t kResetSpyroForGameplay = 0x8004AC24u;
constexpr std::uint32_t kLoadLevel = 0x80015370u;
constexpr std::uint32_t kLoadStageLevelReady = 13u;
// LoadLevel at its final stage finishes a level whose data is already resident, and the Spyro reset
// is a leaf. A budget neither can exhaust is a budget that cannot report the call going wrong.
constexpr std::uint64_t kFlybyTerminalCycles = 32u << 20;

} // namespace

bool flybyCardUp(const TransitionState &state) {
  return state.stage == kStageTitleScreen && state.titleMode == kTitleModeDemo &&
         state.titleState == kTitleStateActive && state.demoType == kDemoTypeLevel;
}

Cancellation classify(const TransitionState &state) {
  if (!state.skipPressed) {
    return Cancellation::None;
  }
  // A tally that has already ended owns no screen to cancel, so the press falls through to whatever
  // reads input next rather than counting as a skip.
  if (state.stage == kStageLevelTransition && state.levelTransHudActive != 0u) {
    return Cancellation::LevelTransitionTally;
  }
  // The return-home glide owns its screen for the whole of stage 10, so unlike the tally there is
  // no separate liveness flag to test: reaching this stage is itself the screen being up.
  if (state.stage == kStageExitLevel) {
    return Cancellation::ReturnHomeSequence;
  }
  // The load gate is the guest's, not a courtesy: GamestateCutsceneTransition runs its terminal
  // only once g_LoadStage reaches 13, and the flyby is what holds the screen while the level
  // streams. A press before then is held by observe and classified on a later frame, so the card is
  // cancelled early but the level is never entered half-loaded.
  if (flybyCardUp(state) && state.loadStage == kLoadStageLevelReady) {
    return Cancellation::CutsceneTransitionFlyby;
  }
  return Cancellation::None;
}

TransitionSkip::TransitionSkip(FieldScheduler &fields) : fields_(fields) {}

void TransitionSkip::observe(Core &core) {
  TransitionState state{.stage = core.mem_r32(kGamestate),
                        .levelTransHudActive = core.mem_r32(kLevelTransHudActive),
                        .titleMode = core.mem_r32(kTitleMode),
                        .titleState = core.mem_r32(kTitleState),
                        .demoType = core.mem_r32(kTitleDemoType),
                        .loadStage = core.mem_r32(kLoadStage),
                        .skipPressed = fields_.presentationSkipPressed()};

  // Start is a single-frame edge, and the flyby's load can outlast it by hundreds of frames.
  // Holding the press is what lets the player press once, early, and still have the card end the
  // instant its level is ready -- as against writing m_Tick, which would end the card before the
  // load and is the fast-forward this class refuses.
  if (!flybyCardUp(state)) {
    flybyPressHeld_ = false;
  } else {
    flybyPressHeld_ = flybyPressHeld_ || state.skipPressed;
    state.skipPressed = flybyPressHeld_;
  }

  switch (classify(state)) {
  case Cancellation::None:
    return;
  case Cancellation::LevelTransitionTally:
    core.mem_w32(kLevelTransHudActive, 0u);
    ++cancellations_;
    // The tally does not gate the level load, it delays it: func_8002DF9C calls LoadLevel(1) while
    // the load is below stage 11 OR this flag is clear, so the screen holds a finished load until
    // the animation ends. Ending it early releases that hold and leaves every load phase to run.
    lucent::info("transition",
                 "level-transition tally cancelled ({}); load continues from stage {}",
                 cancellations_,
                 core.mem_r32(kLoadStage));
    return;
  case Cancellation::ReturnHomeSequence:
    psx::cpu::dispatchGuestToReturn0(core,
                                     kReturnHome,
                                     psx::cpu::ExecutionBudget::fromCycles(kReturnHomeCycles),
                                     "transition-return-home");
    ++cancellations_;
    lucent::info("transition",
                 "return-home glide cancelled ({}); guest 0x{:08X} left stage {} load stage {}",
                 cancellations_,
                 kReturnHome,
                 core.mem_r32(kGamestate),
                 core.mem_r32(kLoadStage));
    return;
  case Cancellation::CutsceneTransitionFlyby:
    flybyPressHeld_ = false;
    psx::cpu::dispatchGuestToReturn1(core,
                                     kResetSpyroForGameplay,
                                     1u,
                                     psx::cpu::ExecutionBudget::fromCycles(kFlybyTerminalCycles),
                                     "transition-flyby-reset-spyro");
    psx::cpu::dispatchGuestToReturn1(core,
                                     kLoadLevel,
                                     1u,
                                     psx::cpu::ExecutionBudget::fromCycles(kFlybyTerminalCycles),
                                     "transition-flyby-load-level");
    ++cancellations_;
    lucent::info("transition",
                 "level flyby cancelled ({}); guest 0x{:08X} then 0x{:08X} left stage {} load "
                 "stage {} title mode {} state {}",
                 cancellations_,
                 kResetSpyroForGameplay,
                 kLoadLevel,
                 core.mem_r32(kGamestate),
                 core.mem_r32(kLoadStage),
                 core.mem_r32(kTitleMode),
                 core.mem_r32(kTitleState));
    return;
  }
}

} // namespace spyro1
