#pragma once

#include "painter_submission_preflight.h"
#include "sparkle_recipe.h"

#include <cstdint>

struct Core;
struct RenderQueue;

namespace spyro::sparkle_submitter {

enum class Status : std::uint8_t {
  Ready,
  NothingToDraw,
  QueueCapacityExceeded,
};

struct Plan {
  Status status = Status::NothingToDraw;
  painter_submission::Plan admission{};
};

Plan prepare(const RenderQueue &queue, const sparkle_recipe::Recipe &recipe);
void submit(Core *core, RenderQueue &queue, const sparkle_recipe::Recipe &recipe, const Plan &plan);

} // namespace spyro::sparkle_submitter
