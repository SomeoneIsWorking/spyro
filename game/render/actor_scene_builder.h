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

// Shared semantic half of the two retail Moby builders. Both 0x8001F158 and
// 0x800208FC transform the same Moby/model state into the same 0x38-byte
// record shape; their source-list ownership is different. Keeping this one
// implementation prevents their culling and matrix formulas from drifting.
bool build_source_record(Core *c,
                         uint32_t moby,
                         actor_recipe_capture::SourceRecord &source,
                         Census &census);

// Builds the regular-actor semantic records directly from the level Moby array, camera, model
// table, and animation state. It replaces 0x800521C0 + 0x8001F158 without running either guest body
// or materializing their temporary guest lists.
Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census);
const char *status_name(Status status);

} // namespace spyro::actor_scene
