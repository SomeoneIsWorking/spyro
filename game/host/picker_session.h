#pragma once

#include "picker_content.h"
#include "picker_runtime.h"

#include <cstdint>
#include <optional>

namespace spyro {

// The selector screen as a session of its own: it owns a Game (window, renderer, pad, control
// channel) for exactly as long as the screen is up, so ending it releases all of them before a
// title boots.
class PickerSession {
public:
  enum class Outcome : std::uint8_t { Chosen, Ended };
  struct Result {
    Outcome outcome = Outcome::Ended;
    const TitleAvailability *title = nullptr; // set when Chosen
  };

  // `frameCap` of 0 runs until a title is chosen; otherwise the screen ends after that many frames.
  PickerSession(PickerRuntime &runtime, const PickerContent &content, int frameCap);

  Result run();

private:
  PickerRuntime &runtime_;
  const PickerContent &content_;
  int frameCap_;
};

} // namespace spyro
