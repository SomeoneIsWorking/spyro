#pragma once

#include "painter_submission_preflight.h"
#include "terrain_recipe.h"

#include <cstdint>

class Core;
struct RenderQueue;

namespace spyro::terrain_submitter {

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

// Named so a refusal can say WHICH preflight condition failed rather than only that one did.
const char *statusName(Status status);

Plan prepare(const RenderQueue &queue, uint32_t producerKey, const terrain_recipe::Recipe &recipe);

// `projection` is the one the recipe's faces were projected through. The depth normalisation the
// render queue wants is a function of the projection plane, so this producer's plane is installed
// for the length of the submission and restored afterwards rather than being left behind as a side
// effect of whichever vertex happened to be projected last.
void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const terrain_recipe::Recipe &recipe,
            const Plan &plan,
            const psxport::native_projection::ProjectionParams &projection);

} // namespace spyro::terrain_submitter
