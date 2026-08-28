#pragma once

#include <cstdint>

class Core;
class Game;

namespace spyro2 {

class Spyro2DisplayBootstrap final {
public:
  explicit Spyro2DisplayBootstrap(Game &game);

  void begin(Core &core);
  void step(Core &core);

  [[nodiscard]] bool complete() const;

private:
  enum class Phase : std::uint8_t {
    FirstField,
    ClearField,
    FinalField,
    Complete,
  };

  void deliverField(Core &core);
  void configureDisplay(Core &core);
  void beginClear(Core &core);
  void clearDisplay(Core &core);
  void finishDisplay(Core &core);

  Game &game_;
  Phase phase_ = Phase::FirstField;
  bool begun_ = false;
};

} // namespace spyro2
