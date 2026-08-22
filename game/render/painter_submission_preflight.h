#pragma once

#include <cstddef>
#include <cstdint>

struct RenderQueue;

namespace spyro::painter_submission {

struct Plan {
  bool ready = false;
  int queued = 0;
  size_t existingObjects = 0;
  size_t existingFaces = 0;
};

// Atomic capacity/shape check shared by native PainterObject producers. No queue state is mutated.
Plan preflight(const RenderQueue &queue,
               uint32_t object,
               size_t newFaces,
               uint32_t replayDomain = 0);

} // namespace spyro::painter_submission
