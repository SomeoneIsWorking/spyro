#pragma once

#include "actor_recipe_capture.h"

#include <cstdint>
#include <vector>

class Core;

namespace spyro::actor_scene {

enum class Status : uint8_t {
  Ready,
  InvalidMobyArray,
  UnterminatedMobyArray,
  RecordCapacityExceeded,
  RecordCaptureRefused,
};

struct Census {
  uint32_t scanned = 0;
  uint32_t queued = 0;
  uint32_t culled = 0;
  uint32_t coarseCulled = 0;
  uint32_t viewCulled = 0;
  uint32_t invalidModel = 0;
};

// Builds the regular-actor semantic records directly from the level Moby array, camera, model
// table, and animation state. It replaces 0x800521C0 + 0x8001F158 without running either guest body
// or materializing their temporary guest lists.
Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census);
const char *status_name(Status status);

} // namespace spyro::actor_scene
