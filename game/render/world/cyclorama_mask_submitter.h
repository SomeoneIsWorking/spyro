#pragma once

#include "cyclorama_mask_recipe.h"
#include "painter_object_layer.h"
#include "painter_submission_preflight.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::cyclorama_mask_submitter {

struct Draw {
  const cyclorama_mask_recipe::Recipe *recipe = nullptr;
};

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidDraw,
  InvalidRecipe,
  InvalidOrder,
  QueueCapacityExceeded,
};

struct FaceRef {
  size_t draw = 0;
  size_t face = 0;
  PainterReplayOrder replay{};
};

struct Plan {
  Status status = Status::ValidEmpty;
  std::vector<FaceRef> faces;
  painter_submission::Plan admission{};
};

Plan prepare(const Core *core, const RenderQueue &queue, std::span<const Draw> draws);
void submit(Core *core, RenderQueue &queue, std::span<const Draw> draws, const Plan &plan);
const char *statusName(Status status);

} // namespace spyro::cyclorama_mask_submitter
