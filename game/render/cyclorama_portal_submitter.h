#pragma once

#include "cyclorama_portal_mesh_recipe.h"
#include "painter_object_layer.h"
#include "painter_submission_preflight.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::cyclorama_portal_submitter {

struct Draw {
  const cyclorama_portal_mesh::PortalFrame *frame = nullptr;
  const cyclorama_portal_mesh::Recipe *recipe = nullptr;
};

enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  InvalidCore,
  InvalidDraw,
  InvalidFamily,
  InvalidAperture,
  InvalidRecipe,
  InvalidOrder,
  QueueCapacityExceeded,
  OrderPrecisionExceeded,
  InvalidDrawArea,
};

struct FaceRef {
  size_t draw = 0;
  size_t face = 0;
  PainterReplayOrder replay{};
};

struct Plan {
  Status status = Status::ValidEmpty;
  uint32_t producerKey = 0;
  std::vector<FaceRef> faces;
  painter_submission::Plan admission{};
};

// Prepare one producer-family batch. The guest loops over portals but reuses each renderer
// function, so all calls for one family must be admitted as one painter object.
Plan prepare(const Core *core,
             const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const Draw> draws);

void submit(Core *core, RenderQueue &queue, std::span<const Draw> draws, const Plan &plan);

const char *statusName(Status status);

} // namespace spyro::cyclorama_portal_submitter
