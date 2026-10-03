// The on-screen test the type-0 point and type-1 line arms of the particle emit list apply to one
// projected endpoint (a line is tested on its first endpoint alone).
//
// Each arm writes its answer into byte +3 of the guest's own particle record, standing in for the
// guest routine it replaces, and then draws. Those are two different questions at 16:9: the byte is
// the guest's, so it takes the guest's 4:3 horizontal window; the draw takes the widened one. The
// line arm used to write the widened answer, so widescreen changed guest memory (issue 0152, the
// same defect issue 0124 fixed for the paired actor).
#pragma once

#include "wide_screen_space.h"

#include <cstdint>

namespace spyro::field_particle_endpoint {

struct Projected {
  uint32_t sz = 0;
  int32_t sx = 0;
  int32_t sy = 0;
};

struct Answer {
  bool guest = false; // the byte written to record +3
  bool drawn = false; // whether this port draws it
  int32_t otDepth = 0;
};

// `offsetDelta` is wide_screen_space::horizontalOffsetDelta for the frame: the drawn x minus the
// guest's x for the same vertex. `drawRight` is wide_screen_space::drawClipRight.
constexpr Answer classify(Projected p, uint32_t depthBias, int32_t offsetDelta, int32_t drawRight) {
  Answer out{};
  out.otDepth = (int32_t)(p.sz >> 5) - (int32_t)depthBias;
  const bool depthAndRow =
      p.sz != 0u && p.sz < 0x2000u && out.otDepth > 2 && p.sy > 0 && p.sy < 256;
  out.guest = depthAndRow &&
              wide_screen_space::onScreenX(p.sx - offsetDelta, wide_screen_space::kGuestClipRight);
  out.drawn = depthAndRow && wide_screen_space::onScreenX(p.sx, drawRight);
  return out;
}

} // namespace spyro::field_particle_endpoint
