#pragma once

#include "game_iface.h"

namespace spyro {

class SpyroRuntime final : public LegacyGameRuntimeAdapter {
public:
  SpyroRuntime();

  void *createContext(Core &core) override;
  void destroyContext(void *context) override;
  void registerOverrides(Game &game) override;
  void bootInit(Core &core) override;
};

} // namespace spyro
