#pragma once

#include "painter_submission_preflight.h"
#include "spyro_flame_recipe.h"

#include <cstddef>

struct Core;
struct RenderQueue;

namespace spyro::flame_submitter {

enum class Status : std::uint8_t {
  Ready,
  NothingToDraw,
  QueueCapacityExceeded,
};

struct Plan {
  Status status = Status::NothingToDraw;
  painter_submission::Plan admission{};
};

Plan prepare(const RenderQueue &queue, std::size_t faceCount);
void submit(Core *core, RenderQueue &queue, const flame_recipe::Recipe &recipe, const Plan &plan);

} // namespace spyro::flame_submitter
