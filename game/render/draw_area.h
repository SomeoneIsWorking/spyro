// Whether the GPU's clipped drawing area can hold anything at all.
//
// The guest sets the draw area as an inclusive rectangle. Retail's GPU draws nothing through an
// inverted one, so a producer that publishes faces into that frame would put geometry in the queue
// that the hardware being reproduced would never have drawn. Seven producers asked the same
// question in the same words before this owner existed, which is seven chances for one of them to
// get the inclusive bound backwards.
#pragma once

#include "gpu_native_internal.h"

namespace spyro::draw_area {

// True when the area is a non-empty inclusive rectangle. Both bounds are inclusive, so an area one
// pixel wide has `x0 == x1` and is ready.
inline bool ready(const GpuState &gpu) {
  return gpu.s_da_x0 <= gpu.s_da_x1 && gpu.s_da_y0 <= gpu.s_da_y1;
}

} // namespace spyro::draw_area
