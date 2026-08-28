#pragma once

#include "painter_submission_preflight.h"
#include "world_recipe.h"

#include <cstddef>
#include <cstdint>
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

struct Plan {
  Status status = Status::ValidEmpty;
  std::vector<size_t> paintOrder;
  painter_submission::Plan admission{};
};

Plan prepare(const Core *core,
             const RenderQueue &queue,
             uint32_t producerKey,
             const world_recipe::Recipe &recipe);
void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const world_recipe::Recipe &recipe,
            const Plan &plan);

} // namespace spyro::world_scene_submitter
