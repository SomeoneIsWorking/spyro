#include "boot_prefix_frame_driver.h"

#include "core.h"
#include "game.h"
#include "spyro_context.h"

#include "execution_exit.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro {
namespace {

// The boot prefix is finite but long: it brings up the display, walks the loader chain and
// dispatches the loaded module.
constexpr std::uint32_t kGuestRamLow = 0x80000000u;
constexpr std::uint32_t kGuestRamHigh = 0x80200000u;

constexpr std::uint32_t kBootTurnsPerStep = 4;
constexpr std::uint32_t kFrameTurnsPerStep = 8;
// A step ends at the first display wait a call stops at, so this only bounds a loop whose
// iteration waits for nothing.
constexpr int kMaxLoopCallsPerStep = 4;

} // namespace

BootPrefixFrameDriver::BootPrefixFrameDriver(Game &game,
                                             const BootPrefixFacts &facts,
                                             spyro::FieldObserver *observer,
                                             spyro::FrameTailObserver *tail)
    : core_(game.core), facts_(facts), fields_(game, facts.field, observer), tail_(tail) {}

void BootPrefixFrameDriver::initialize() {
  if (initialized_) {
    lucent::error("boot-native", "{} frame driver initialized twice", facts_.titleName);
    std::abort();
  }
  initialized_ = true;
  fields_.publish();
  if (!enterCall(facts_.bootPrefix)) {
    // Refused before any guest work; the run is stalled by name rather than aborted.
    bootStalled_ = true;
    core_.game->run.requestEnd();
    return;
  }
  lucent::info("boot-native",
               "{} enters the retail boot prefix 0x{:08X}; libetc VSync stays a frame boundary",
               facts_.titleName,
               facts_.bootPrefix);
}

bool BootPrefixFrameDriver::enterCall(std::uint32_t entry) {
  // A host-entered call has no guest caller to return to, so `$r[31]` carries whatever the host
  // last had there and the executor ends the call when the guest PC EQUALS it. A host pointer
  // cannot equal a KSEG0 guest PC, which is what makes the call finite.
  const std::uint32_t returnPc = core_.r[31];
  if (returnPc >= kGuestRamLow && returnPc < kGuestRamHigh) {
    lucent::error(
        "boot-native",
        "{}: refusing to enter {} with `$r[31]` = 0x{:08X} inside guest RAM; the call would "
        "stop at an address it never reaches",
        facts_.titleName,
        facts_.callName,
        returnPc);
    return false;
  }
  // The turn cap is the title's bound; this driver ends a call on its own field and step bounds.
  call_.begin(core_, facts_.callName, entry, returnPc, psx::cpu::kUnboundedCallTurns);
  callTurns_ = 0;
  callCycles_ = 0;
  return true;
}

void BootPrefixFrameDriver::deliverField(const char *site, bool present) {
  if (!fields_.deliver({.site = site, .present = present, .pace = present})) {
    lucent::error("frameloop", "{}: field owner refused the field at {}", facts_.titleName, site);
    std::abort();
  }
}

void BootPrefixFrameDriver::reportStop(Core &core,
                                       const psx::cpu::ResumableGuestCall &call,
                                       const psx::cpu::CallStep &step) const {
  lucent::error("frameloop",
                "{} stopped: phase={} step={} call 0x{:08X} active={} turns={} cycles={} "
                "exit={} at guest pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} fields={} presents={}",
                facts_.titleName,
                phase_ == Phase::Boot ? "boot" : "main-loop",
                steps_,
                call.entry(),
                call.pending() ? 1 : 0,
                callTurns_,
                callCycles_,
                step.detail.empty() ? "-" : step.detail,
                step.guestPc,
                core.r[31],
                core.r[29],
                fields_.fields(),
                fields_.presents());
}

BootPrefixFrameDriver::CallProgress
BootPrefixFrameDriver::runCall(Core &core,
                               psx::cpu::ResumableGuestCall &call,
                               std::uint32_t maxTurns,
                               const char *site,
                               std::uint64_t fieldBound) {
  for (std::uint32_t turn = 0; turn < maxTurns;) {
    const psx::cpu::CallStep step = call.advance();
    ++callTurns_;
    callCycles_ += step.cycles;
    if (step.outcome == psx::cpu::CallOutcome::Returned) {
      return CallProgress::Returned;
    }
    if (step.outcome == psx::cpu::CallOutcome::Refused) {
      reportStop(core, call, step);
      std::abort();
    }
    if (step.reason == psx::cpu::ExecutionExitReason::FrameBoundary) {
      // A guest display wait: real time and the guest's own vblank work, not presented.
      deliverField(site, false);
      if (fields_.fields() >= fieldBound) {
        // The guest answers every display wait with another, so it will never return.
        return CallProgress::FieldBoundReached;
      }
      continue;
    }
    // A suspension that is not a display wait is one host turn without a field: the step's turn
    // budget, not the field bound.
    return CallProgress::TurnBudgetReached;
  }
  return CallProgress::TurnBudgetReached;
}

void BootPrefixFrameDriver::stepBoot(Core &core) {
  const std::uint64_t fieldsBefore = fields_.fields();
  // Checked first, before any guest work, because a guest that polls instead of asking for fields
  // never reaches the delivery site the field bound lives on.
  if (bootSteps_ >= facts_.bootStepLimit) {
    bootStalled_ = true;
    lucent::error("frameloop",
                  "{}'s boot prefix ran {} step(s) without returning and delivered {} "
                  "field(s) in total (step bound {}); it is polling rather than waiting, so the "
                  "field bound cannot see it -- ending the run at resume 0x{:08X}",
                  facts_.titleName,
                  bootSteps_,
                  fields_.fields(),
                  facts_.bootStepLimit,
                  call_.resumePc());
    core.game->run.requestEnd();
    // Not an early return: a step presents its one field whatever the guest did.
  }
  // The bound is a budget for THIS call, not the step's running total: a boot that legitimately
  // delivers 200 fields across ten steps must not be refused at step ten for having delivered 200.
  // A refused-at-entry boot holds no call at all, which is what distinguishes it here.
  if (bootStalled_ && call_.entry() == 0u) {
    return;
  }
  const auto progress =
      bootStalled_ ? CallProgress::TurnBudgetReached
                   : runCall(core, call_, kBootTurnsPerStep, "boot", facts_.bootStepFieldLimit);
  if (progress == CallProgress::Returned) {
    phase_ = Phase::MainLoop;
    lucent::info("boot-native",
                 "{}'s retail boot prefix returned after {} step(s) and {} field(s); the "
                 "per-frame update 0x{:08X} and draw 0x{:08X} now own each step",
                 facts_.titleName,
                 steps_,
                 fields_.fields(),
                 facts_.frameUpdate,
                 facts_.frameDraw);
  } else if (progress == CallProgress::FieldBoundReached) {
    // A boot prefix that spends its whole field budget without returning is not making progress, so
    // the run ends by name instead of spinning on it.
    bootStalled_ = true;
    lucent::error("frameloop",
                  "{}'s boot prefix delivered {} field(s) in one call without returning and "
                  "gave up on the guest at resume 0x{:08X} (bound {}); ending the run",
                  facts_.titleName,
                  fields_.fields() - fieldsBefore,
                  call_.resumePc(),
                  facts_.bootStepFieldLimit);
    core.game->run.requestEnd();
  } else {
    ++bootSteps_;
  }
}

std::uint32_t BootPrefixFrameDriver::entryOf(LoopCall call) const {
  return call == LoopCall::Update ? facts_.frameUpdate : facts_.frameDraw;
}

const char *BootPrefixFrameDriver::siteOf(LoopCall call) const {
  return call == LoopCall::Update ? "frame-update" : "frame-draw";
}

void BootPrefixFrameDriver::stepMainLoop(Core &core) {
  // One step advances the retail loop to its next display wait: the update, then the draw that
  // carries the frame's wait, resuming whichever call the previous step left suspended. Resuming a
  // suspended draw as the update would leave the update unreachable after its first iteration.
  for (int calls = 0; calls < kMaxLoopCallsPerStep; ++calls) {
    if (!call_.pending() && !enterCall(entryOf(loopCall_))) {
      return;
    }
    if (runCall(core, call_, kFrameTurnsPerStep, siteOf(loopCall_), fields_.fields() + 1u) !=
        CallProgress::Returned) {
      return;
    }
    if (loopCall_ == LoopCall::Draw && tail_ != nullptr) {
      tail_->onFrameDrawn(core);
    }
    loopCall_ = loopCall_ == LoopCall::Update ? LoopCall::Draw : LoopCall::Update;
  }
}

void BootPrefixFrameDriver::stepFrame(Core &core, std::uint32_t frame) {
  if (!initialized_) {
    lucent::error(
        "frameloop", "{} step {} ran before boot initialization", facts_.titleName, frame);
    std::abort();
  }
  ++steps_;
  if (phase_ == Phase::Boot) {
    stepBoot(core);
  } else {
    stepMainLoop(core);
  }

  // The title's frame-tail work: the guest's last command of the step has executed, the step's
  // queue is not yet rasterised.
  if (tail_ != nullptr) {
    tail_->onFrameTail(core);
  }

  // Exactly one presentation fence per step; its last field is the visible one.
  deliverField(phase_ == Phase::Boot ? "boot-tail" : "frame-tail", true);
  // Without a logic-frame clock OtAttr never drops its packet spans and attributes reused pool
  // words to stale writers.
  core.rsub.otAttr.beginLogicFrame(frame);

  lucent::debug("boot",
                "{} step {} phase={} call_active={} resume=0x{:08X} turns={} cycles={} "
                "fields={} presents={}",
                facts_.titleName,
                frame,
                phase_ == Phase::Boot ? "boot" : "main-loop",
                call_.pending() ? 1 : 0,
                call_.resumePc(),
                callTurns_,
                callCycles_,
                fields_.fields(),
                fields_.presents());
}

BootPrefixFrameDriver &bootPrefixFrameDriver(Core &core) {
  if (core.game == nullptr || core.game->frameDriver == nullptr) {
    lucent::error("frameloop", "boot-prefix runtime has no FrameDriver");
    std::abort();
  }
  return static_cast<BootPrefixFrameDriver &>(*core.game->frameDriver);
}

} // namespace spyro
