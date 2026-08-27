#pragma once

#include "game_runtime.h"
#include "spyro1_boot_sequence.h"
#include "spyro1_field_scheduler.h"
#include "spyro1_frame_policy.h"

#include <cstdint>
#include <memory>

class Game;
class SpyroRenderer;

namespace spyro1 {

class Spyro1FrameDriver final : public FrameDriver {
public:
  explicit Spyro1FrameDriver(Game &game);
  ~Spyro1FrameDriver() override;

  void initialize(Core &core);
  void stepFrame(Core &core, std::uint32_t frame) override;

  FieldScheduler &fields();
  const FieldScheduler &fields() const;

private:
  FieldScheduler fields_;
  BootSequence boot_;
  std::unique_ptr<SpyroRenderer> renderer_;
  std::uint32_t gameplayFrame_ = 0;
};

Spyro1FrameDriver &frameDriver(Core &core);
const Spyro1FrameDriver &frameDriver(const Core &core);

} // namespace spyro1
