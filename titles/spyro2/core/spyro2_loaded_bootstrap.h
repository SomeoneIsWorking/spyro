#pragma once

#include "spyro2_loaded_bootstrap_timing.h"

class Core;
class Game;

namespace spyro2 {

class Spyro2LoadedBootstrap final {
public:
  explicit Spyro2LoadedBootstrap(Game &game);

  void begin(Core &core);
  void step(Core &core);

  [[nodiscard]] bool complete() const;

private:
  void deliverField(Core &core);
  void runUntil(Core &core, std::uint32_t start, std::uint32_t stop);
  void queryCounter(Core &core, std::uint32_t resume, std::uint32_t stop);
  void beginChild(Core &core, std::uint32_t returnAddress);
  void resumeChild(Core &core, std::uint32_t stop);
  void beginClear(Core &core, std::uint32_t returnAddress);
  void resumeClear(Core &core, std::uint32_t stop);
  void validateRetainedTimingMap(Core &core) const;
  void handleTransition(Core &core, LoadedBootstrapTransition transition);

  Game &game_;
  Spyro2LoadedBootstrapTiming timing_;
  std::uint32_t fieldsDelivered_ = 0u;
  bool begun_ = false;
};

} // namespace spyro2
