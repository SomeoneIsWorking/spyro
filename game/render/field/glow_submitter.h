#pragma once

#include "glow_recipe.h"
#include "painter_submission_preflight.h"

#include <cstddef>

struct Core;
struct RenderQueue;

namespace spyro::glow_submitter {

enum class Status : std::uint8_t {
  Ready,
  NothingToDraw,
  QueueCapacityExceeded,
};

struct Plan {
  Status status = Status::NothingToDraw;
  painter_submission::Plan admission{};
};

Plan prepare(const RenderQueue &queue, const glow_recipe::Recipe &recipe);
void submit(Core *core, RenderQueue &queue, const glow_recipe::Recipe &recipe, const Plan &plan);

} // namespace spyro::glow_submitter
