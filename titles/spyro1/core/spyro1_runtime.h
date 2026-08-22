#pragma once

#include "spyro_runtime.h"

namespace spyro1 {

// Runtime owner for SCUS_942.28. The legacy views remain bound only for framework consumers that
// have not yet migrated from GameConfig; executable identity and lifecycle live on this type.
class Spyro1Runtime final : public spyro::SpyroRuntime {
public:
  Spyro1Runtime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;

private:
  static const GuestProgramImage programImage_;
};

} // namespace spyro1
