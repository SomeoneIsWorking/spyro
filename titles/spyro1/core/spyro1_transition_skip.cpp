#include "spyro1_transition_skip.h"

#include "core.h"
#include "guest_call.h"
#include "spyro1_field_scheduler.h"

#include <lucent/log.h>

namespace spyro1 {
namespace {

// g_Gamestate. Stage 1 is GS_LevelTransition, whose update is func_8002DF9C.
constexpr std::uint32_t kStageSelector = 0x800757D8u;
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

// g_LoadStage, reported so a cancellation can be read against the load it did not touch.
constexpr std::uint32_t kLoadStage = 0x80075864u;

} // namespace

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
  return Cancellation::None;
}

TransitionSkip::TransitionSkip(FieldScheduler &fields) : fields_(fields) {}

void TransitionSkip::observe(Core &core) {
  const TransitionState state{.stage = core.mem_r32(kStageSelector),
                              .levelTransHudActive = core.mem_r32(kLevelTransHudActive),
                              .skipPressed = fields_.presentationSkipPressed()};
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
                 core.mem_r32(kStageSelector),
                 core.mem_r32(kLoadStage));
    return;
  }
}

} // namespace spyro1
