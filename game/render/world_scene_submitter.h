#pragma once

#include "painter_submission_preflight.h"
#include "world_recipe.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::world_scene_submitter {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidRecipe,
  InvalidOrder,
  QueueCapacityExceeded,
  OrderPrecisionExceeded,
  InvalidDrawArea,
};

// Immutable destination and raster policy admitted for this submission. Geometry/material
// choices remain in Recipe; later GPU commands cannot retarget an already prepared draw.
struct DrawState {
  int offsetX = 0, offsetY = 0;
  int areaLeft = 0, areaTop = 0, areaRight = 0, areaBottom = 0;
  int windowMaskX = 0, windowMaskY = 0, windowOffsetX = 0, windowOffsetY = 0;
  int dither = 0;
  uint16_t projectionH = 0;
};

struct Plan {
  Status status = Status::ValidEmpty;
  std::vector<size_t> paintOrder;
  painter_submission::Plan admission{};
  DrawState draw{};
};

std::optional<DrawState> captureDrawState(Core &core);
Plan prepare(const DrawState &draw,
             const RenderQueue &queue,
             uint32_t producerKey,
             const world_recipe::Recipe &recipe);

Plan prepare(Core *core,
             const RenderQueue &queue,
             uint32_t producerKey,
             const world_recipe::Recipe &recipe);
// Logic-frame submission publishes the complete guest visibility table, including empty output.
void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const world_recipe::Recipe &recipe,
            const Plan &plan);

// Presentation-only emission consumes the same admitted recipe without writing guest state.
// False means the plan cannot be submitted; a valid empty recipe succeeds with no queue output.
bool emit(Core *core,
          RenderQueue &queue,
          uint32_t producerKey,
          const world_recipe::Recipe &recipe,
          const Plan &plan);

} // namespace spyro::world_scene_submitter
