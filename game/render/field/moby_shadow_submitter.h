#pragma once

#include "moby_shadow_recipe.h"
#include "painter_submission_preflight.h"

#include <cstddef>
#include <cstdint>

struct Core;
struct RenderQueue;

namespace spyro::moby_shadow_submitter {

enum class Status : std::uint8_t { Ready, InvalidRecipe, InvalidMaterial, QueueCapacityExceeded };

struct Plan {
  Status status = Status::InvalidRecipe;
  painter_submission::Plan admission{};
  // The one shadow tile every Moby shadow shares, resolved from g_MobyShadows before any face is
  // published so a missing tile refuses the whole producer instead of drawing untextured blobs.
  int mode = 0;
  int tpX = 0;
  int tpY = 0;
  int clutX = 0;
  int clutY = 0;
  int blend = 0;
  int dither = 0;
  std::uint8_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

Plan prepare(Core *core, const RenderQueue &queue, std::size_t faceCount);
void submit(Core *core,
            RenderQueue &queue,
            const moby_shadow_recipe::Recipe &recipe,
            const Plan &plan);

} // namespace spyro::moby_shadow_submitter
