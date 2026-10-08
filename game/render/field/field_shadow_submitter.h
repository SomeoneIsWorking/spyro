#pragma once

#include "field_shadow_recipe.h"
#include "painter_submission_preflight.h"

#include <cstddef>
#include <cstdint>

struct Core;
struct RenderQueue;

namespace spyro::field_shadow_submitter {

enum class Status : std::uint8_t { Ready, InvalidRecipe, QueueCapacityExceeded };

struct Plan {
  Status status = Status::InvalidRecipe;
  painter_submission::Plan admission{};
};

Plan prepare(const RenderQueue &queue, std::size_t faceCount);
void submit(Core *core,
            RenderQueue &queue,
            const field_shadow_recipe::Recipe &recipe,
            const Plan &plan);

} // namespace spyro::field_shadow_submitter
