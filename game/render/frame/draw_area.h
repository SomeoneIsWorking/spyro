// An inverted draw area draws nothing on the retail GPU, so producers must ask before publishing.
#pragma once

#include "gpu_native_internal.h"

namespace spyro::draw_area {

// Both bounds are inclusive, so an area one pixel wide has x0 == x1.
inline bool ready(const GpuState &gpu) {
  return gpu.s_da_x0 <= gpu.s_da_x1 && gpu.s_da_y0 <= gpu.s_da_y1;
}

} // namespace spyro::draw_area
