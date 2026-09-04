#pragma once

#include "spyro_runtime.h"

namespace spyro3 {

// Process-lifetime owner of SCUS_944.67's measured executable facts. It deliberately binds no
// Spyro 1 compatibility state, context, or native overrides.
class Spyro3Runtime final : public spyro::SpyroRuntime {
public:
  Spyro3Runtime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;
  bool guestVramIsPicture(const Game &game) const override;

private:
  static const GuestProgramImage programImage_;
};

} // namespace spyro3
