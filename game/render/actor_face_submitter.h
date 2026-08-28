#pragma once

#include "actor_draw_recipe.h"
#include "actor_global_order.h"
#include "painter_submission_preflight.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

class Core;
struct RenderQueue;

namespace spyro::actor_face_submitter {

enum class Layer : uint8_t { Regular, Secondary };
enum class Status : uint8_t {
  Ready,
  ValidEmpty,
  UnsupportedMaterial,
  InvalidGlobalOrder,
  QueueCapacityExceeded,
};

struct Material {
  bool textured = false;
  bool semiTransparent = false;
  uint16_t clut = 0;
  uint16_t tpage = 0;
  std::array<uint32_t, 4> attributes{};
};

struct Plan {
  Status status = Status::ValidEmpty;
  std::vector<actor_global_order::FaceKey> replay;
  std::vector<Material> materials;
  painter_submission::Plan admission{};
};

// Resolves materials, global OT replay order, and queue capacity without
// mutating the queue or guest state. A producer may publish its guest-side
// builder effects only after this returns Ready/ValidEmpty.
Plan prepare(const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const actor_prefix::Output> records,
             std::span<const actor_draw_recipe::Face> faces);

// Publishes an already-preflighted plan. The caller owns draw-area validation
// and any atomic guest-state commit immediately before this operation.
void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            Layer layer,
            std::span<const actor_draw_recipe::Face> faces,
            const Plan &plan);

} // namespace spyro::actor_face_submitter
