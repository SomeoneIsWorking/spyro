#pragma once

#include "game_runtime.h"
#include "spyro2_display_bootstrap.h"
#include "spyro2_loaded_bootstrap.h"

class Game;

namespace spyro2 {

class Spyro2FrameDriver final : public FrameDriver {
public:
  explicit Spyro2FrameDriver(Game &game);

  void initialize(Core &core);
  void stepFrame(Core &core, std::uint32_t frame) override;

  [[nodiscard]] bool reachedDisplayBootstrap() const;

private:
  Spyro2DisplayBootstrap display_;
  Spyro2LoadedBootstrap loadedBootstrap_;
  bool initialized_ = false;
  bool reachedDisplayBootstrap_ = false;
  bool spuBootstrapComplete_ = false;
  bool cdBootstrapComplete_ = false;
  bool cdMusicInitialized_ = false;
  bool geometryInitialized_ = false;
  bool archiveLoaded_ = false;
};

Spyro2FrameDriver &frameDriver(Core &core);

} // namespace spyro2
