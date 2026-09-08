#pragma once

#include "field_shaded_queue_recipe.h"
#include "painter_submission_preflight.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::field_shaded_queue_submitter {

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidRecipe,
  InvalidOrder,
  QueueCapacityExceeded,
};

struct Plan {
  Status status = Status::ValidEmpty;
  painter_submission::Plan admission{};
};

Plan prepare(const RenderQueue &queue,
             uint32_t producerKey,
             const field_shaded_queue_recipe::Recipe &recipe);
void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const field_shaded_queue_recipe::Recipe &recipe,
            const Plan &plan);

} // namespace spyro::field_shaded_queue_submitter
