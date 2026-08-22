#pragma once

#include "game_runtime.h"

namespace spyro {

// Engine-lineage runtime root. Each serial owns its immutable executable image and behavior in a
// derived title runtime; this base prevents one title's GameConfig from becoming another title's
// identity by accident.
class SpyroRuntime : public GameRuntime {
public:
  const GuestProgramImage *guestProgramImage() const final;

protected:
  explicit SpyroRuntime(const GuestProgramImage &programImage);

private:
  const GuestProgramImage &programImage_;
};

} // namespace spyro
