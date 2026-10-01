#pragma once

#include "field_owner.h"
#include "game_runtime.h"
#include "spyro_guest_call.h"

#include <cstdint>

class Core;
class Game;

namespace spyro {

// The fields a boot prefix may deliver in one call, and the steps it may spend, before the run
// calls it stalled. Two bounds because they catch different failures: a guest that answers every
// display wait with another one is seen where the field is delivered, while a guest that polls a
// status word and never asks for a field is invisible there and only exhausts the turn budget.
// A title overrides a limit only with its own measurement beside it.
inline constexpr std::uint64_t kDefaultBootStepFieldLimit = 1024;
inline constexpr std::uint64_t kDefaultBootStepLimit = 480;

// Everything that differs between titles that run their retail boot prefix and then the retail
// update/draw pair: each address is measured from the title's own executable and each limit
// carries its measurement where the facts are declared.
struct BootPrefixFacts {
  const char *titleName = "";
  // The name the title's guest call reports itself under.
  const char *callName = "";
  // The retail boot prefix, entered once as a finite guest call.
  std::uint32_t bootPrefix = 0;
  // The retail per-frame pair game main loops over once the prefix has returned.
  std::uint32_t frameUpdate = 0;
  std::uint32_t frameDraw = 0;
  FieldOwnerFacts field;
  std::uint64_t bootStepFieldLimit = kDefaultBootStepFieldLimit;
  std::uint64_t bootStepLimit = kDefaultBootStepLimit;
};

// One product step of a title whose retail executable IS its boot.
//
// The title has no native renderer and no hand-built boot. What the host owns is the FIELD, not
// the code: every guest VSync reaches the framework's measured libetc boundary, whose body never
// executes, and comes back as a typed frame-boundary exit this driver satisfies by delivering a
// field through the shared field owner. One product step presents exactly one field, which is the
// framework's frame contract.
//
// The host-turn clock is deliberately NOT armed. The guest's own VSync is the only field source the
// boot uses, and a second one would deliver a field the guest never asked for inside a step that
// already presents one.
class BootPrefixFrameDriver final : public FrameDriver {
public:
  // The title's per-field observer, or null for a title with nothing to observe. It is a
  // constructor argument rather than part of `BootPrefixFacts` because the facts are measured BYTES
  // and the observer is an OBJECT with a lifetime; a facts struct that carried a pointer to one
  // would make the measured table responsible for something it cannot own.
  //
  // `tail` is the same idea at the step's other end: the point between the guest's last work and
  // the field being presented. See `FrameTailObserver` for why that is a separate moment.
  BootPrefixFrameDriver(Game &game,
                        const BootPrefixFacts &facts,
                        spyro::FieldObserver *observer = nullptr,
                        spyro::FrameTailObserver *tail = nullptr);

  void initialize();
  void stepFrame(Core &core, std::uint32_t frame) override;

  [[nodiscard]] bool bootComplete() const {
    return phase_ == Phase::MainLoop;
  }

  [[nodiscard]] bool bootStalled() const {
    return bootStalled_;
  }

  [[nodiscard]] FieldOwner &fields() {
    return fields_;
  }

  [[nodiscard]] const FieldOwner &fields() const {
    return fields_;
  }

  [[nodiscard]] const BootPrefixFacts &facts() const {
    return facts_;
  }

private:
  enum class Phase : std::uint8_t { Boot, MainLoop };
  enum class CallProgress : std::uint8_t { Returned, TurnBudgetReached, FieldBoundReached };
  // The two retail calls of one main-loop iteration, in the order game main runs them.
  enum class LoopCall : std::uint8_t { Update, Draw };

  // One guest call, a bounded number of host turns, and one presented field. `fieldBound` is the
  // number of fields this ONE call may have the host deliver before the step calls it stalled, and
  // it is enforced HERE rather than in the step: a guest that answers every display wait with
  // another display wait never returns, so a bound checked after this loop would never be reached.
  CallProgress runCall(Core &core,
                       GuestCall &call,
                       std::uint32_t maxTurns,
                       const char *site,
                       std::uint64_t fieldBound);
  void deliverField(const char *site, bool present);
  void stepBoot(Core &core);
  void stepMainLoop(Core &core);
  [[nodiscard]] std::uint32_t entryOf(LoopCall call) const;
  [[nodiscard]] const char *siteOf(LoopCall call) const;
  void reportStop(Core &core, const GuestCall &call, const GuestCall::Result &result) const;

  BootPrefixFacts facts_;
  FieldOwner fields_;
  spyro::FrameTailObserver *tail_ = nullptr;
  GuestCall call_;
  Phase phase_ = Phase::Boot;
  // Which retail call the guest call in `call_` is, or is about to be, once the main loop runs. A
  // call suspended at a display wait is resumed by a later step, so the step cannot assume it holds
  // the update: it holds whichever call the loop reached.
  LoopCall loopCall_ = LoopCall::Update;
  bool initialized_ = false;
  bool bootStalled_ = false;
  std::uint64_t steps_ = 0;
  std::uint64_t bootSteps_ = 0;
};

BootPrefixFrameDriver &bootPrefixFrameDriver(Core &core);

} // namespace spyro
