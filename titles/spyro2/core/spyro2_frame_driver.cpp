#include "spyro2_frame_driver.h"

#include "core.h"
#include "game.h"
#include "host_turn.h"
#include "runtime_run.h"
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro2 {
namespace {

// MEASURED from SCUS_944.25 with external/psxport/tools/disasm.py over a RAM image built the
// PS-X EXE way (file[0x800] -> t_addr 0x80010000). The bytes are quoted so each fact can be
// re-derived rather than trusted.
//
//   80011E9C  addiu $sp,$sp,-0x18 ; sw $ra,0x14($sp)   boot prefix entry
//   80011EA4  jal  0x800548A4                          first leaf (display/libc init)
//   80011EAC  jal  0x80011BBC                          display bootstrap: VSync(0) at
//   80011BD0  jal  0x80058EDC                          libetc 0x80058EDC, then three fields
//   80011EB4  jal  0x80011B1C                          leaf the retired bootstrap stopped at
//   80011EBC  jal  0x80011B3C                          CD bootstrap
//   80011EC4  jal  0x80012B84                          music init
//   80011ECC  jal  0x80011D24                          geometry init
//   80011EF8  jal  0x80013810                          module load
//   80011F0C  jal  0x80077374  (a0=1)                  the loaded module's entry
constexpr std::uint32_t kBootPrefix = 0x80011E9Cu;
//   80011AEC  jal  0x8001B140                          game main's per-frame update
constexpr std::uint32_t kFrameUpdate = 0x8001B140u;
//   80011AF4  jal  0x800156FC                          game main's draw, after the update
constexpr std::uint32_t kFrameDraw = 0x800156FCu;
//   80011AFC  jal  0x800156FC ; 80011B04  j 0x80011AF4  the retail loop is these two and nothing
//   else
constexpr std::uint32_t kGameMain = 0x80011ADCu;

// The boot prefix is finite but long: it brings up the display, walks the loader chain and
// dispatches the loaded module. It is given a generous per-step turn budget so one boot step can
// carry real work, and the field boundaries inside it are delivered as they are reached.
constexpr std::uint32_t kBootTurnsPerStep = 4;
constexpr std::uint32_t kFrameTurnsPerStep = 8;

} // namespace

Spyro2FrameDriver::Spyro2FrameDriver(Game &game)
    : fields_(game,
              spyro::FieldOwnerFacts{
                  .titleName = "Spyro 2",
                  // The guest word the host advances once per delivered field. Zero says this
                  // title has no measured field counter of its own yet, so the host owns no
                  // guest word and only the framework's presentation fence counts fields; it is
                  // stated rather than invented.
                  .fieldCounter = 0,
                  // No guest IRQ root is claimed: SCUS_944.25 installs the BIOS HookEntryInt
                  // continuation as its libetc vblank callback (0x80054A78 stores B(0x19) and
                  // 0x80054A84 registers it through VSyncCallback 0x8005AC34), which the
                  // framework's own interrupt path resumes.
                  .rootHandlerSlot = 0,
                  .handlerStackTop = 0x8000E000u,
                  .handlerStackBytes = 8192u,
                  .fieldsPerLogicFrame = 2,
              }),
      call_(game.core, "spyro2-step") {}

void Spyro2FrameDriver::initialize(Core &core) {
  if (initialized_) {
    lucent::error("boot-native", "Spyro 2 frame driver initialized twice");
    std::abort();
  }
  initialized_ = true;
  fields_.publish();
  call_.begin(kBootPrefix);
  lucent::info("boot-native",
               "Spyro 2 enters the retail boot prefix 0x{:08X}; game main 0x{:08X} is NOT "
               "dispatched and libetc VSync 0x80058EDC stays a frame boundary",
               kBootPrefix,
               kGameMain);
  (void)core;
}

void Spyro2FrameDriver::deliverField(Core &core, const char *site, bool present) {
  if (!fields_.deliver({.site = site, .present = present, .pace = present})) {
    lucent::error("frameloop", "{}: field owner refused the field at {}", "Spyro 2", site);
    std::abort();
  }
}

void Spyro2FrameDriver::reportStop(Core &core,
                                   const GuestCall &call,
                                   const GuestCall::Result &result) const {
  lucent::error("frameloop",
                "Spyro 2 stopped: phase={} step={} call 0x{:08X} active={} turns={} cycles={} "
                "exit={} at guest pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} fields={} presents={}",
                phase_ == Phase::Boot ? "boot" : "main-loop",
                steps_,
                call.entry(),
                call.active() ? 1 : 0,
                call.turns(),
                call.cycles(),
                result.detail.empty() ? "-" : result.detail,
                result.guestPc,
                core.r[31],
                core.r[29],
                fields_.fields(),
                fields_.presents());
}

Spyro2FrameDriver::CallProgress Spyro2FrameDriver::runCall(Core &core,
                                                           GuestCall &call,
                                                           std::uint32_t maxTurns,
                                                           const char *site,
                                                           std::uint64_t fieldBound) {
  for (std::uint32_t turn = 0; turn < maxTurns;) {
    const auto result = call.advance(1);
    switch (result.outcome) {
    case GuestCall::Outcome::FieldBoundary:
      // A guest display wait. The field is real time and the guest's own vblank work runs with
      // it; it is not presented, because this step's single presentation fence is its own.
      deliverField(core, site, false);
      if (fields_.fields() >= fieldBound) {
        // The guest answers every display wait with another display wait, so it will never return.
        // Stopping HERE is the only place that ends it, and the number that stopped it is in the
        // refusal so nobody has to guess which bound fired.
        return CallProgress::FieldBoundReached;
      }
      continue;
    case GuestCall::Outcome::Returned:
      return CallProgress::Returned;
    case GuestCall::Outcome::BudgetExhausted:
      return CallProgress::TurnBudgetReached;
    case GuestCall::Outcome::Fault:
      reportStop(core, call, result);
      std::abort();
    }
  }
  return CallProgress::TurnBudgetReached;
}

void Spyro2FrameDriver::stepFrame(Core &core, std::uint32_t frame) {
  if (!initialized_) {
    lucent::error("frameloop", "Spyro 2 step {} ran before boot initialization", frame);
    std::abort();
  }
  ++steps_;
  const std::uint64_t fieldsBefore = fields_.fields();

  if (phase_ == Phase::Boot) {
    // Checked FIRST, before any guest work, because a guest that polls instead of asking for
    // fields never reaches the delivery site the field bound lives on. `bootSteps_` counts steps
    // this call has already run without returning.
    if (bootSteps_ >= bootStepBound_) {
      bootStalled_ = true;
      lucent::error("frameloop",
                    "Spyro 2's boot prefix ran {} step(s) without returning and delivered {} "
                    "field(s) in total (step bound {}); it is polling rather than waiting, so the "
                    "field bound cannot see it -- ending the run at resume 0x{:08X}",
                    bootSteps_,
                    fields_.fields(),
                    bootStepBound_,
                    call_.resumePc());
      spyro::runtimeRun(core).requestEnd();
      // NOT an early return: one product step is one presented field whatever the guest did, and
      // the framework's frame driver checks exactly that. Returning here aborted the process on
      // `presentation fence advanced 0 time(s), expected exactly 1` -- a contract abort on the very
      // step whose job was to name the stall, which hid the diagnosis it had just printed.
    }
    // The bound is a budget for THIS call, not for the step's running total: a boot that
    // legitimately delivers 200 fields across ten steps must not be refused at step ten for having
    // delivered 200.
    //
    // No guest work runs on a step that has already refused the guest; the step still presents its
    // one field below, which is what keeps the frame contract true on the last step of a run.
    const auto progress =
        bootStalled_ ? CallProgress::TurnBudgetReached
                     : runCall(core, call_, kBootTurnsPerStep, "boot", bootStepFieldBound_);
    if (progress == CallProgress::Returned) {
      phase_ = Phase::MainLoop;
      lucent::info("boot-native",
                   "Spyro 2's retail boot prefix returned after {} step(s) and {} field(s); the "
                   "per-frame update 0x{:08X} and draw 0x{:08X} now own each step",
                   steps_,
                   fields_.fields(),
                   kFrameUpdate,
                   kFrameDraw);
    } else if (progress == CallProgress::FieldBoundReached) {
      // A boot prefix that spends its whole field budget without returning is a guest that is not
      // making progress, and the run ends by name rather than spinning on it. `requestEnd` is the
      // framework's own completion path, so the process still reaches its shutdown accounting
      // instead of dying inside a loop.
      bootStalled_ = true;
      lucent::error("frameloop",
                    "Spyro 2's boot prefix delivered {} field(s) in one call without returning and "
                    "gave up on the guest at resume 0x{:08X} (bound {}); ending the run",
                    fields_.fields() - fieldsBefore,
                    call_.resumePc(),
                    bootStepFieldBound_);
      spyro::runtimeRun(core).requestEnd();
    } else {
      ++bootSteps_;
    }
  } else {
    // One product step is one drawn retail iteration: the gamestate update, then the draw that
    // carries the frame's display wait. The update is finite and must return inside its step; a
    // call that outlives the step is resumed by the next one, which is why the draw follows only
    // once the update has actually returned.
    if (!call_.active()) {
      call_.begin(kFrameUpdate);
    }
    if (runCall(core, call_, kFrameTurnsPerStep, "frame-update", fields_.fields() + 1u) ==
        CallProgress::Returned) {
      if (!call_.active()) {
        call_.begin(kFrameDraw);
      }
      runCall(core, call_, kFrameTurnsPerStep, "frame-draw", fields_.fields() + 1u);
    }
  }

  // Exactly one presentation fence per product step, which is the framework's frame contract.
  // The step's last field is the visible one, so whatever the guest drew during it is what the
  // player sees.
  deliverField(core, phase_ == Phase::Boot ? "boot-tail" : "frame-tail", true);

  lucent::debug("boot",
                "Spyro 2 step {} phase={} call_active={} resume=0x{:08X} turns={} cycles={} "
                "fields={} presents={}",
                frame,
                phase_ == Phase::Boot ? "boot" : "main-loop",
                call_.active() ? 1 : 0,
                call_.resumePc(),
                call_.turns(),
                call_.cycles(),
                fields_.fields(),
                fields_.presents());
}

Spyro2FrameDriver &frameDriver(Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("frameloop", "Spyro 2 runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<Spyro2FrameDriver &>(*core.game->frameDriver);
}

} // namespace spyro2
