#pragma once

#include "field_owner.h"
#include "game_runtime.h"
#include "spyro_guest_call.h"

#include <cstdint>

class Core;
class Game;

namespace spyro2 {

// One product step of SCUS_944.25.
//
// The title has no native renderer and no hand-built boot: the retail executable IS the boot.
// Game main 0x80011ADC calls the boot prefix 0x80011E9C, which brings up the display through
// libetc VSync 0x80058EDC and then runs the loader chain, and the per-frame loop is the retail
// pair 0x8001B140 (the gamestate dispatch) followed by 0x800156FC (the draw, which owns the
// frame's display wait).
//
// What the host owns is the FIELD, not the code: every guest VSync reaches the framework's
// measured libetc boundary, whose body never executes, and comes back as a typed
// frame-boundary exit this driver satisfies by delivering a field through the shared field
// owner. One product step presents exactly one field, which is the framework's frame contract.
//
// The host-turn clock is deliberately NOT armed. Spyro 1's boot needs it because its native
// scheduler owns the wait; here the guest's own VSync is the only field source the boot uses, and
// a second one would deliver a field the guest never asked for inside a step that already
// presents one. Measured over 30 boot fields: every field is a guest display wait, and the
// product step count (27) is below the field count (30) rather than above it, which is what a
// starved host clock would not produce.
//
// The finite display bootstrap this replaces (three hand-owned fields and a deliberate stop at
// boot-prefix leaf 0x80011B1C) is gone. It reproduced the first three fields of a guest boot in
// native code and then refused to continue, so the only thing it knew that this does not is
// which three fields those were.
class Spyro2FrameDriver final : public FrameDriver {
public:
  explicit Spyro2FrameDriver(Game &game);

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

  // A boot step that has delivered this many fields without the prefix returning is a guest that
  // is not making progress, not a slow one. It is a member of this class rather than a file
  // constant so the enforcement site and the number are the same declaration, and the number is in
  // the refusal so nobody has to guess which bound fired.
  // MEASURED 2026-10-01: the retail boot prefix, once its loaded module is published as an image
  // (game/core/stock_read_publication.*), returns after 133 fields in 43 steps. The first version
  // of this bound was 64, a guess made while the boot stopped at the module load, and it ended a
  // healthy boot mid-fade. 1024 is the nearest power of two above the measured count with margin; a
  // boot that needs more is a different boot and should move this number with a new measurement.
  static constexpr std::uint64_t kDefaultBootStepFieldLimit = 1024;
  // The other way a boot prefix can fail to return is to never ASK for a field at all: a guest
  // polling a CD status word spends every turn inside one `advance` and only ever exhausts the
  // turn budget. The field bound above is checked where a field is delivered, so it never fires
  // for such a guest and the step loop would run until the process's own frame cap -- which is a
  // run length, not a diagnosis. This bound is checked at the top of every boot step instead, and
  // the refusal names which of the two fired.
  static constexpr std::uint64_t kDefaultBootStepLimit = 480;

private:
  enum class Phase : std::uint8_t { Boot, MainLoop };
  enum class CallProgress : std::uint8_t { Returned, TurnBudgetReached, FieldBoundReached };

  // One guest call, a bounded number of host turns, and one presented field. `fieldBound` is the
  // number of fields this ONE call may have the host deliver before the step calls it stalled, and
  // it is enforced HERE rather than in the step: a guest that answers every display wait with
  // another display wait never returns, so a bound checked after this loop would never be reached.
  CallProgress runCall(Core &core,
                       spyro::GuestCall &call,
                       std::uint32_t maxTurns,
                       const char *site,
                       std::uint64_t fieldBound);
  void deliverField(Core &core, const char *site, bool present);
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
  // The per-step field bound a boot prefix may spend before the step calls it stalled. It is a
  // policy input rather than a constant so a test can observe the refusal through the same code
  // path a wedged guest takes, instead of asserting that a constant equals itself.
  void setBootStepFieldBound(std::uint64_t fields) {
    bootStepFieldBound_ = fields;
  }

  // The same, for the step bound: a boot prefix that never returns and never asks for a field.
  void setBootStepBound(std::uint64_t steps) {
    bootStepBound_ = steps;
  }

  // The bound that stopped the boot, or 0 when it has not stopped. The two bounds are different
  // failures and a reader must be able to tell them apart.
  [[nodiscard]] std::uint64_t bootStepLimit() const {
    return bootStepBound_;
  }

  [[nodiscard]] bool bootStalled() const {
    return bootStalled_;
  }
};

Spyro2FrameDriver &frameDriver(Core &core);

} // namespace spyro2
