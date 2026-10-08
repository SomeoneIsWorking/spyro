// The guest's own render arm, run unmodified, as the owner of a scene the native producers do not
// compose. The seam is the arm's first field wait: frameBegin/frameEnd already are the render
// driver's head and its display tail.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

class Core;

namespace spyro::render {

// What the guest's own render arm did with one frame's scene. This is a typed answer, never a
// process exit: a scene with no native recipe is a gap in the port, not a fault in the product.
enum class GuestSceneStop : std::uint8_t {
  Drawn,         // the arm returned through its own `jr $ra`: its whole scene is submitted
  AskedForField, // the arm reached its display tail's field wait: its whole scene is submitted and
                 // the port owns the rest of the frame
  Refused,       // a typed exit that is neither of those. Named, counted, and never fatal.
};

struct GuestSceneStep {
  GuestSceneStop stop = GuestSceneStop::Refused;
  std::uint32_t arm = 0;     // the guest render arm that was run
  std::uint32_t guestPc = 0; // where the arm stopped
  std::uint64_t cycles = 0;  // guest cycles it consumed
  std::string reason;        // the executor's own words for the stop
};

// Run one guest render arm, unmodified, through the runtime executor, for at most one display
// field. `arm` is the handler address the scene classifier recovered. Never aborts, never throws.
GuestSceneStep drawSceneWithGuestArm(Core &core, std::uint32_t arm);

} // namespace spyro::render