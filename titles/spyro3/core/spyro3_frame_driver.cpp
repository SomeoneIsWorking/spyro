#include "spyro3_frame_driver.h"

#include "core.h"
#include "game.h"
#include "runtime_run.h"
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro3 {
namespace {

// MEASURED from SCUS_944.67 with external/psxport/tools/disasm.py over a RAM image built the
// PS-X EXE way (file[0x800] -> t_addr 0x80010000). The bytes are quoted so each fact can be
// re-derived rather than trusted.
//
//   8001200C  addiu $sp,$sp,-0x18 ; sw $ra,0x10($sp)  game main
//   80012014  jal  0x800594EC                          constructor table walk
//   8001201C  jal  0x8002AB38                          the boot prefix, which never returns
//   80012024  jal  0x80055400                          the per-frame update, and the loop head
//   8001202C  jal  0x8001E638                          the draw, after the update
//   80012034  j    0x80012024                          the retail loop is these two and nothing
//   else
constexpr std::uint32_t kGameMain = 0x8001200Cu;
constexpr std::uint32_t kBootPrefix = 0x8002AB38u;
constexpr std::uint32_t kFrameUpdate = 0x80055400u;
constexpr std::uint32_t kFrameDraw = 0x8001E638u;

//   8002AB38  addiu $sp,$sp,-0x40 ; sw $ra,0x3c($sp)  boot prefix entry
//   8002AB48  jal  0x8005C684                          first leaf; 0x8005C684 loads the table
//                                                        pointer at 0x8006B350 and does `jalr` on
//                                                        its word 3, so it dispatches through the
//                                                        guest's own handler table
//   8002AB50  jal  0x8002A834                          display bootstrap
//   8002A848  jal  0x8005956C  (a0 = 0)               its VSync(0), the first display wait
//   8002AB58  jal  0x8002A794
//   8002AB60  jal  0x8002A7B4                          CD bootstrap, and inside it
//   8002A7C0    jal 0x8005DB1C                           CdInit
//   8002A7D0    jal 0x8005E0BC  (a0 = 0x0E)             CdCommand(CdlReadS)
//   8002A7E0    jal 0x8005DB08  (a0 = 0x80050504)       CdReadyCallback, this title's own
//                                                        per-sector reader
//   8002AB68  jal  0x8004F8EC
//   8002AB70  jal  0x8002A99C                          geometry init, which calls SetGeomOffset
//                                                        at 0x8002A9B0 with (0x100, 0x78)
//   8002ABA4  jal  0x80050578
//   8002ACB4  jal  0x80074DEC  (a0 = 1)                a loaded module's entry, OUTSIDE the
//   resident text: this port never seeds it and never dispatches it
//
// The two leaves this file does NOT name as constants are named in the runtime's measured plan
// instead, because that is where they are owned: 0x8005D35C (SetGeomOffset) and the libcd window.

// The boot prefix is finite but long: it brings up the display, walks the loader chain and
// dispatches the loaded module. It gets a generous per-step turn budget so one boot step can
// carry real work, and the field boundaries inside it are delivered as they are reached.
constexpr std::uint32_t kBootTurnsPerStep = 4;
constexpr std::uint32_t kFrameTurnsPerStep = 8;

} // namespace

Spyro3FrameDriver::Spyro3FrameDriver(Game &game)
    : fields_(game,
              spyro::FieldOwnerFacts{
                  .titleName = "Spyro 3",
                  // The guest word the host advances once per delivered field. Zero says this
                  // title has no measured field counter of its own yet, so the host owns no
                  // guest word and only the framework's presentation fence counts fields; it is
                  // stated rather than invented.
                  .fieldCounter = 0,
                  // No guest IRQ root is claimed: no vblank root handler slot has been measured
                  // for SCUS_944.67, so the framework's own interrupt path owns the callback.
                  .rootHandlerSlot = 0,
                  .handlerStackTop = 0x8000E000u,
                  .handlerStackBytes = 8192u,
                  // The shared field cadence this lineage's titles declare. It is NOT a Spyro 3
                  // measurement: no per-frame field count has been read out of SCUS_944.67 yet, and
                  // this driver never arms the host field clock, so nothing in the boot depends on
                  // the number. It is stated here rather than left to look measured.
                  .fieldsPerLogicFrame = 2,
              }),
      call_(game.core, "spyro3-step") {}

void Spyro3FrameDriver::initialize(Core &core) {
  if (initialized_) {
    lucent::error("boot-native", "Spyro 3 frame driver initialized twice");
    std::abort();
  }
  initialized_ = true;
  fields_.publish();
  call_.begin(kBootPrefix);
  lucent::info("boot-native",
               "Spyro 3 enters the retail boot prefix 0x{:08X}; game main 0x{:08X} is NOT "
               "dispatched and libetc VSync 0x8005956C stays a frame boundary",
               kBootPrefix,
               kGameMain);
  (void)core;
}

void Spyro3FrameDriver::deliverField(const char *site, bool present) {
  if (!fields_.deliver({.site = site, .present = present, .pace = present})) {
    lucent::error("frameloop", "{}: field owner refused the field at {}", "Spyro 3", site);
    std::abort();
  }
}

void Spyro3FrameDriver::reportStop(Core &core,
                                   const spyro::GuestCall &call,
                                   const spyro::GuestCall::Result &result) const {
  lucent::error("frameloop",
                "Spyro 3 stopped: phase={} step={} call 0x{:08X} active={} turns={} cycles={} "
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

Spyro3FrameDriver::CallProgress Spyro3FrameDriver::runCall(Core &core,
                                                           spyro::GuestCall &call,
                                                           std::uint32_t maxTurns,
                                                           const char *site,
                                                           std::uint64_t fieldBound) {
  for (std::uint32_t turn = 0; turn < maxTurns;) {
    const auto result = call.advance(1);
    switch (result.outcome) {
    case spyro::GuestCall::Outcome::FieldBoundary:
      // A guest display wait. The field is real time and the guest's own vblank work runs with
      // it; it is not presented, because this step's single presentation fence is its own.
      deliverField(site, false);
      if (fields_.fields() >= fieldBound) {
        // The guest answers every display wait with another display wait, so it will never
        // return. Stopping HERE is the only place that ends it, and the number that stopped it
        // is in the refusal so nobody has to guess which bound fired.
        return CallProgress::FieldBoundReached;
      }
      continue;
    case spyro::GuestCall::Outcome::Returned:
      return CallProgress::Returned;
    case spyro::GuestCall::Outcome::BudgetExhausted:
      return CallProgress::TurnBudgetReached;
    case spyro::GuestCall::Outcome::Fault:
      reportStop(core, call, result);
      std::abort();
    }
  }
  return CallProgress::TurnBudgetReached;
}

void Spyro3FrameDriver::stepFrame(Core &core, std::uint32_t frame) {
  if (!initialized_) {
    lucent::error("frameloop", "Spyro 3 step {} ran before boot initialization", frame);
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
                    "Spyro 3's boot prefix ran {} step(s) without returning and delivered {} "
                    "field(s) in total (step bound {}); it is polling rather than waiting, so the "
                    "field bound cannot see it -- ending the run at resume 0x{:08X}",
                    bootSteps_,
                    fields_.fields(),
                    bootStepBound_,
                    call_.resumePc());
      spyro::runtimeRun(core).requestEnd();
      // NOT an early return: one product step is one presented field whatever the guest did, and
      // the framework's frame driver checks exactly that.
    }
    // The bound is a budget for THIS call, not for the step's running total: a boot that
    // legitimately delivers 200 fields across ten steps must not be refused at step ten for
    // having delivered 200. No guest work runs on a step that has already refused the guest;
    // the step still presents its one field below, which keeps the frame contract true on the
    // last step of a run.
    const auto progress =
        bootStalled_ ? CallProgress::TurnBudgetReached
                     : runCall(core, call_, kBootTurnsPerStep, "boot", bootStepFieldBound_);
    if (progress == CallProgress::Returned) {
      phase_ = Phase::MainLoop;
      lucent::info("boot-native",
                   "Spyro 3's retail boot prefix returned after {} step(s) and {} field(s); the "
                   "per-frame update 0x{:08X} and draw 0x{:08X} now own each step",
                   steps_,
                   fields_.fields(),
                   kFrameUpdate,
                   kFrameDraw);
    } else if (progress == CallProgress::FieldBoundReached) {
      // A boot prefix that spends its whole field budget without returning is a guest that is
      // not making progress, and the run ends by name rather than spinning on it. `requestEnd`
      // is the framework's own completion path, so the process still reaches its shutdown
      // accounting instead of dying inside a loop.
      bootStalled_ = true;
      lucent::error("frameloop",
                    "Spyro 3's boot prefix delivered {} field(s) in one call without returning and "
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
    // call that outlives the step is resumed by the next one, which is why the draw follows
    // only once the update has actually returned.
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
  deliverField(phase_ == Phase::Boot ? "boot-tail" : "frame-tail", true);

  lucent::debug("boot",
                "Spyro 3 step {} phase={} call_active={} resume=0x{:08X} turns={} cycles={} "
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

Spyro3FrameDriver &frameDriver(Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("frameloop", "Spyro 3 runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<Spyro3FrameDriver &>(*core.game->frameDriver);
}

} // namespace spyro3
