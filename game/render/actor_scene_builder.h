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
  InvalidShadowCursor,
};

struct Census {
  uint32_t scanned = 0;
  uint32_t queued = 0;
  uint32_t culled = 0;
  uint32_t coarseCulled = 0;
  uint32_t viewCulled = 0;
  uint32_t invalidModel = 0;
};

struct Shadow {
  uint32_t moby = 0;
  uint32_t modelByte = 0;
};

struct Frame {
  std::vector<actor_recipe_capture::Record> records;
  std::vector<Shadow> shadows;
  Census census{};
  uint32_t shadowCursor = 0;
};

// Shared semantic half of the two retail Moby builders. Both 0x8001F158 and
// 0x800208FC transform the same Moby/model state into the same 0x38-byte
// record shape; their source-list ownership is different. Keeping this one
// implementation prevents their culling and matrix formulas from drifting.
bool build_source_record(Core *c,
                         uint32_t moby,
                         actor_recipe_capture::SourceRecord &source,
                         Census &census,
                         bool *horizontalVisibleOut = nullptr);

// Builds the regular-actor records and the shadow-list entries produced by the same retail
// culling pass. The frame is inert until commit succeeds in the owning submitter.
Status build_frame(Core *c, Frame &frame);
void commit(Core *c, const Frame &frame);

// 0x8001F344 and 0x8001F350 guard the shadow-list append with two `bgez` branches that both SKIP:
// the Moby's m_ShadowDistance must be negative, and its view depth must be nearer than the staging
// limit. The depth is a positive quantity, so the sign belongs on the limit rather than on the
// depth; negating it instead makes the pair unsatisfiable and silently stages no shadow at all.
bool stages_shadow(int32_t shadowWord, int32_t viewZ);
constexpr int32_t kShadowStagingDepth = 0x1200;

// Builds the regular-actor semantic records directly from the level Moby array, camera, model
// table, and animation state. It replaces 0x800521C0 + 0x8001F158 without running either guest body
// or materializing their temporary guest lists.
Status build_records(Core *c, std::vector<actor_recipe_capture::Record> &records, Census &census);
const char *status_name(Status status);

} // namespace spyro::actor_scene
