#pragma once

#include "field_owner.h"
#include "game_runtime.h"
#include "resumable_guest_call.h"

#include <cstdint>

class Core;
class Game;

namespace spyro {

// Two bounds catch different stalls: a guest that answers every display wait with another one
// burns the field limit, a guest that never asks for a field only exhausts the turn budget.
inline constexpr std::uint64_t kDefaultBootStepFieldLimit = 1024;
inline constexpr std::uint64_t kDefaultBootStepLimit = 480;

// Titles whose retail executable IS their boot; each address is measured from that executable.
struct BootPrefixFacts {
  const char *titleName = "";
  const char *callName = "";
  std::uint32_t bootPrefix = 0;
  std::uint32_t frameUpdate = 0;
  std::uint32_t frameDraw = 0;
  FieldOwnerFacts field;
  std::uint64_t bootStepFieldLimit = kDefaultBootStepFieldLimit;
  std::uint64_t bootStepLimit = kDefaultBootStepLimit;
};

// One product step of a title whose retail executable IS its boot. The host owns the FIELD: every
// guest VSync comes back as a frame-boundary exit this driver satisfies with one delivered field,
// so one step presents exactly one field. The host-turn clock stays unarmed because the guest's
// own VSync is the only field source the boot uses.
class BootPrefixFrameDriver final : public FrameDriver {
public:
  // The title's per-field observer, or null; the facts struct carries measured bytes, not objects
  // with a lifetime. `tail` observes the step's other end, the point before the field is presented.
  BootPrefixFrameDriver(Game &game,
                        const BootPrefixFacts &facts,
                        spyro::FieldObserver *observer = nullptr,
                        spyro::FrameTailObserver *tail = nullptr);

  void initialize();
  void stepFrame(Core &core, std::uint32_t frame) override;

  // True once the retail boot prefix (logos, publisher cards, loading) has returned.
  [[nodiscard]] bool pastBootPrefix() const override {
    return phase_ == Phase::MainLoop;
  }

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
  // Why one retail call ended: it returned, ran out of host turns, or hit the field bound.
  enum class CallProgress : std::uint8_t { Returned, TurnBudgetReached, FieldBoundReached };
  // The two retail calls of one main-loop iteration, in the order game main runs them.
  enum class LoopCall : std::uint8_t { Update, Draw };

  // Fields this ONE call may have the host deliver before the step calls it stalled; checked here,
  // because a guest that never returns would never reach a bound checked after the loop.
  CallProgress runCall(Core &core,
                       psx::cpu::ResumableGuestCall &call,
                       std::uint32_t maxTurns,
                       const char *site,
                       std::uint64_t fieldBound);
  // False when `$r[31]` would end the call in the middle of itself; no guest work runs.
  bool enterCall(std::uint32_t entry);
  void deliverField(const char *site, bool present);
  void stepBoot(Core &core);
  void stepMainLoop(Core &core);
  [[nodiscard]] std::uint32_t entryOf(LoopCall call) const;
  [[nodiscard]] const char *siteOf(LoopCall call) const;
  void reportStop(Core &core,
                  const psx::cpu::ResumableGuestCall &call,
                  const psx::cpu::CallStep &step) const;

  Core &core_;
  BootPrefixFacts facts_;
  FieldOwner fields_;
  spyro::FrameTailObserver *tail_ = nullptr;
  psx::cpu::ResumableGuestCall call_;
  Phase phase_ = Phase::Boot;
  // Which retail call `call_` is, once the main loop runs: a call suspended at a display wait is
  // resumed later, so the step cannot assume it holds the update.
  LoopCall loopCall_ = LoopCall::Update;
  bool initialized_ = false;
  bool bootStalled_ = false;
  std::uint64_t steps_ = 0;
  std::uint64_t bootSteps_ = 0;
  // Host turns and guest cycles `call_` has consumed across every step it was suspended in; the
  // step log is a per-step question, `Core::guestCallCensus()` answers the per-run one.
  std::uint64_t callTurns_ = 0;
  std::uint64_t callCycles_ = 0;
};

BootPrefixFrameDriver &bootPrefixFrameDriver(Core &core);

} // namespace spyro
