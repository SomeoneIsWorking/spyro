#pragma once

#include "picker_content.h"
#include "picker_runtime.h"

#include "gpu_vk_device.h"

#include <cstdint>
#include <optional>

namespace spyro {

// The selector screen as a session of its own: it owns a Game (machine, renderer state, pad,
// control channel) for exactly as long as the screen is up, so ending it releases all of them
// before a title boots. It does NOT own the window or the presentation device — the product does
// (see ProductHost), and `presentation` is that device, passed by reference.
class PickerSession {
public:
  enum class Outcome : std::uint8_t { Chosen, Ended };
  struct Result {
    Outcome outcome = Outcome::Ended;
    const TitleAvailability *title = nullptr; // set when Chosen
  };

  // `frameCap` of 0 runs until a title is chosen; otherwise the screen ends after that many frames.
  // `presentation` must outlive this session.
  PickerSession(PickerRuntime &runtime,
                const PickerContent &content,
                int frameCap,
                GpuDevice &presentation);

  Result run();

private:
  PickerRuntime &runtime_;
  const PickerContent &content_;
  int frameCap_;
  GpuDevice &presentation_;
};

} // namespace spyro
