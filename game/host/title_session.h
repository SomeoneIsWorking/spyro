#pragma once

#include "title_availability.h"

#include "gpu_vk_device.h"

#include <cstdint>

namespace spyro {

// One boot-to-exit run of one title: constructs the Game, boots the authenticated executable, runs
// the frame loop and tears everything down in reverse order. Destruction is the whole teardown, so
// the next session starts from a process with nothing of this one left in it — except the window
// and the presentation device, which the product owns (`presentation`) so they survive into the
// next session. `presentation` must outlive this session.
class TitleSession {
public:
  enum class End : std::uint8_t {
    ReturnedToSelector, // the player (or `session return`) asked for the title selector
    Finished,           // the frame cap was reached or the run ended itself
    Refused,            // the executable could not be loaded
  };

  TitleSession(const TitleAvailability &title, bool selectorAvailable, GpuDevice &presentation);

  End run();

private:
  const TitleAvailability &title_;
  bool selectorAvailable_;
  GpuDevice &presentation_;
};

} // namespace spyro
