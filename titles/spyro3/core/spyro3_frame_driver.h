#pragma once

#include "field_owner.h"
#include "game_runtime.h"
#include "spyro_guest_call.h"

#include <cstdint>

class Core;
class Game;

namespace spyro3 {

// One product step of SCUS_944.67.
//
// The title has no native renderer and no hand-built boot: the retail executable IS the boot.
// Game main 0x8001200C walks the constructor table at 0x800594EC, enters the retail boot prefix
// 0x8002AB38, and its per-frame loop is the retail pair 0x80055400 (the update) followed by
// 0x8001E638 (the draw, which is the leaf carrying this title's display waits).
//
// What the host owns is the FIELD, not the code: every guest VSync reaches the framework's
// measured libetc boundary at 0x8005956C, whose body never executes, and comes back as a typed
// frame-boundary exit this driver satisfies by delivering a field through the shared field
// owner. One product step presents exactly one field, which is the framework's frame contract.
//
// The host-turn clock is deliberately NOT armed, for the same reason as Spyro 2's driver: the
// guest's own VSync is the only field source this boot uses, and a second one would deliver a
// field the guest never asked for inside a step that already presents one.
//
// The two boot bounds are different failures and both are named in their refusal: `kBootStepLimit`
// is checked at the TOP of every boot step and catches a guest that polls instead of waiting,
// while `kDefaultBootStepFieldLimit` is checked where a field is delivered and catches a guest
// that answers every display wait with another one.
class Spyro3FrameDriver final : public FrameDriver {
public:
  explicit Spyro3FrameDriver(Game &game);

  void initialize(Core &core);
  void stepFrame(Core &core, std::uint32_t frame) override;

  [[nodiscard]] bool bootComplete() const {
    return phase_ == Phase::MainLoop;
  }

  [[nodiscard]] spyro::FieldOwner &fields() {
    return fields_;
  }

  [[nodiscard]] const spyro::FieldOwner &fields() const {
    return fields_;
  }

  // The per-step field bound a boot prefix may spend before the step calls it stalled. It is a
  // policy input rather than a constant so a test can observe the refusal through the same code
  // path a wedged guest takes, instead of asserting that a constant equals itself.
  // MEASURED 2026-10-01: the retail boot prefix, once its loaded module is published as an image
  // (game/core/stock_read_publication.*), returns after 546 fields in 336 steps. The first version
  // of this bound was 64, a guess made while the boot stopped at the module load, and it ended a
  // healthy boot mid-fade. 1024 is the nearest power of two above the measured count with margin; a
  // boot that needs more is a different boot and should move this number with a new measurement.
  static constexpr std::uint64_t kDefaultBootStepFieldLimit = 1024;
  // The bound checked at the top of every boot step: a prefix that never returns and never asks
  // for a field at all, so the field bound above can never see it.
  static constexpr std::uint64_t kDefaultBootStepLimit = 480;

private:
  enum class Phase : std::uint8_t { Boot, MainLoop };
  enum class CallProgress : std::uint8_t { Returned, TurnBudgetReached, FieldBoundReached };

  CallProgress runCall(Core &core,
                       spyro::GuestCall &call,
                       std::uint32_t maxTurns,
                       const char *site,
                       std::uint64_t fieldBound);
  // No Core argument: the shared field owner already holds the Game, and a parameter here would
  // be carried through the hot path unused.
  void deliverField(const char *site, bool present);
  void reportStop(Core &core,
                  const spyro::GuestCall &call,
                  const spyro::GuestCall::Result &result) const;

  spyro::FieldOwner fields_;
  spyro::GuestCall call_;
  Phase phase_ = Phase::Boot;
  bool initialized_ = false;
  bool bootStalled_ = false;
  std::uint64_t steps_ = 0;
  std::uint64_t bootSteps_ = 0;
  std::uint64_t bootStepFieldBound_ = kDefaultBootStepFieldLimit;
  std::uint64_t bootStepBound_ = kDefaultBootStepLimit;

public:
  void setBootStepFieldBound(std::uint64_t fields) {
    bootStepFieldBound_ = fields;
  }

  void setBootStepBound(std::uint64_t steps) {
    bootStepBound_ = steps;
  }

  // The step bound that stopped the boot, or 0 when it has not stopped. The two bounds are
  // different failures and a reader must be able to tell them apart.
  [[nodiscard]] std::uint64_t bootStepLimit() const {
    return bootStepBound_;
  }

  [[nodiscard]] bool bootStalled() const {
    return bootStalled_;
  }
};

Spyro3FrameDriver &frameDriver(Core &core);

} // namespace spyro3
