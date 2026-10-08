#pragma once

#include "spyro1_press_latch.h"

#include <cstdint>

class Core;

namespace spyro1 {

class FieldScheduler;

// Resumable transcription of Spyro 1 boot 0x800127C0/0x8001286C. Each incomplete step performs
// exactly one 60 Hz field; zero-field asset/finalization transitions are folded into an adjacent
// step. The retained generated bodies remain available for differential diagnostics but are not
// dispatched by the product boot because they call libetc VSync.
class BootSequence {
public:
  explicit BootSequence(FieldScheduler &fields);

  void initialize(Core &core);
  bool step(Core &core);
  bool complete() const;

private:
  enum class Phase : std::uint8_t {
    FadeFirstIn,
    LoadAssets,
    HoldFirst,
    FadeFirstOut,
    FadeSecondIn,
    AdvanceLoadState,
    HoldSecond,
    FadeSecondOut,
    Finalize,
    Complete,
  };

  void deliverField(Core &core, const char *site);
  void drawLogoField(Core &core, std::uint32_t source, std::uint32_t destination, int offset);
  void loadAssets(Core &core);
  void finalize(Core &core);
  void leaveFirstPresentationHold(Core &core);
  void leaveSecondPresentationHold();
  void observePress();

  FieldScheduler &fields_;
  Phase phase_ = Phase::FadeFirstIn;
  std::uint32_t originalStack_ = 0;
  std::uint32_t firstHoldStart_ = 0;
  std::uint32_t secondHoldStart_ = 0;
  std::uint32_t assetBase_ = 0;
  std::uint32_t logoSource_ = 0;
  std::uint32_t logoDestination_ = 0;
  // A Start/Cross edge seen in a fade or in the stage 3->10 loader, held until a hold phase can act
  // on it. The fades and the loader still run to their own ends; only the hold's exit is taken
  // early.
  PressLatch pressLatch_;
  int iteration_ = 0;
  bool initialized_ = false;
};

} // namespace spyro1
