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
  // painter_object_layer.h's PainterObjectAdmissionRefusal and the queue item it refused at, so a
  // refusal names its cause instead of only that it happened.
  int refusal = 0;
  size_t refusalItem = SIZE_MAX;
};

// Atomic capacity/shape check shared by native PainterObject producers. No queue state is mutated.
Plan preflight(const RenderQueue &queue,
               uint32_t object,
               size_t newFaces,
               uint32_t replayDomain = 0);

} // namespace spyro::painter_submission
