#include "boot_prefix_frame_driver.h"

#include "core.h"
#include "game.h"
#include "runtime_run.h"
#include "spyro_context.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro {
namespace {

// The boot prefix is finite but long: it brings up the display, walks the loader chain and
// dispatches the loaded module. It is given a generous per-step turn budget so one boot step can
// carry real work, and the field boundaries inside it are delivered as they are reached.
constexpr std::uint32_t kBootTurnsPerStep = 4;
constexpr std::uint32_t kFrameTurnsPerStep = 8;
// Guest calls one main-loop step may complete. A step ends at the first display wait a call stops
// at, so this only bounds a loop whose iteration waits for nothing (which would otherwise spin
// inside one step): two full iterations is enough for a step to finish the suspended call and
// reach the next wait.
constexpr int kMaxLoopCallsPerStep = 4;

} // namespace

BootPrefixFrameDriver::BootPrefixFrameDriver(Game &game, const BootPrefixFacts &facts)
    : facts_(facts), fields_(game, facts.field), call_(game.core, facts.callName) {}

void BootPrefixFrameDriver::initialize() {
  if (initialized_) {
    lucent::error("boot-native", "{} frame driver initialized twice", facts_.titleName);
    std::abort();
  }
  initialized_ = true;
  fields_.publish();
  call_.begin(facts_.bootPrefix);
  lucent::info("boot-native",
               "{} enters the retail boot prefix 0x{:08X}; libetc VSync stays a frame boundary",
               facts_.titleName,
               facts_.bootPrefix);
}

void BootPrefixFrameDriver::deliverField(const char *site, bool present) {
  if (!fields_.deliver({.site = site, .present = present, .pace = present})) {
    lucent::error("frameloop", "{}: field owner refused the field at {}", facts_.titleName, site);
    std::abort();
  }
}

void BootPrefixFrameDriver::reportStop(Core &core,
                                       const GuestCall &call,
                                       const GuestCall::Result &result) const {
  lucent::error("frameloop",
                "{} stopped: phase={} step={} call 0x{:08X} active={} turns={} cycles={} "
                "exit={} at guest pc=0x{:08X} ra=0x{:08X} sp=0x{:08X} fields={} presents={}",
                facts_.titleName,
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

BootPrefixFrameDriver::CallProgress BootPrefixFrameDriver::runCall(Core &core,
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
      deliverField(site, false);
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

void BootPrefixFrameDriver::stepBoot(Core &core) {
  const std::uint64_t fieldsBefore = fields_.fields();
  // Checked FIRST, before any guest work, because a guest that polls instead of asking for fields
  // never reaches the delivery site the field bound lives on. `bootSteps_` counts steps this call
  // has already run without returning.
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
    runtimeRun(core).requestEnd();
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
  // one field, which is what keeps the frame contract true on the last step of a run.
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
    // A boot prefix that spends its whole field budget without returning is a guest that is not
    // making progress, and the run ends by name rather than spinning on it. `requestEnd` is the
    // framework's own completion path, so the process still reaches its shutdown accounting
    // instead of dying inside a loop.
    bootStalled_ = true;
    lucent::error("frameloop",
                  "{}'s boot prefix delivered {} field(s) in one call without returning and "
                  "gave up on the guest at resume 0x{:08X} (bound {}); ending the run",
                  facts_.titleName,
                  fields_.fields() - fieldsBefore,
                  call_.resumePc(),
                  facts_.bootStepFieldLimit);
    runtimeRun(core).requestEnd();
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
  // One product step advances the retail loop to its next display wait: the update, then the draw
  // that carries the frame's wait, resuming whichever call the previous step left suspended. The
  // loop therefore remembers WHICH call is in flight. Resuming a suspended draw as though it were
  // the update, and then starting the draw again, left the update unreachable after its first
  // iteration, so the world never advanced (issue 0159).
  for (int calls = 0; calls < kMaxLoopCallsPerStep; ++calls) {
    if (!call_.active()) {
      call_.begin(entryOf(loopCall_));
    }
    if (runCall(core, call_, kFrameTurnsPerStep, siteOf(loopCall_), fields_.fields() + 1u) !=
        CallProgress::Returned) {
      return;
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

  // Exactly one presentation fence per product step, which is the framework's frame contract.
  // The step's last field is the visible one, so whatever the guest drew during it is what the
  // player sees.
  deliverField(phase_ == Phase::Boot ? "boot-tail" : "frame-tail", true);

  lucent::debug("boot",
                "{} step {} phase={} call_active={} resume=0x{:08X} turns={} cycles={} "
                "fields={} presents={}",
                facts_.titleName,
                frame,
                phase_ == Phase::Boot ? "boot" : "main-loop",
                call_.active() ? 1 : 0,
                call_.resumePc(),
                call_.turns(),
                call_.cycles(),
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
