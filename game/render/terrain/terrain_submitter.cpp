#include "terrain_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "proj_params.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <cstdlib>
#include <lucent/log.h>

namespace spyro::terrain_submitter {
namespace {

// Installs this producer's projection plane for the length of a submission. The depth the render
// queue stores is normalised against the plane distance, and every other producer has its own.
class ProjectionPlaneScope {
public:
  ProjectionPlaneScope(ProjParams &parameters, uint16_t plane)
      : parameters_(parameters), restore_(parameters.snapshot()) {
    parameters_.setProjH(plane);
  }
  ProjectionPlaneScope(const ProjectionPlaneScope &) = delete;
  ProjectionPlaneScope &operator=(const ProjectionPlaneScope &) = delete;
  ~ProjectionPlaneScope() {
    parameters_.restore(restore_);
  }

private:
  ProjParams &parameters_;
  ProjParams::Snapshot restore_;
};

} // namespace

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "Ready";
  case Status::ValidEmpty:
    return "ValidEmpty";
  case Status::InvalidRecipe:
    return "InvalidRecipe";
  case Status::InvalidOrder:
    return "InvalidOrder";
  case Status::QueueCapacityExceeded:
    return "QueueCapacityExceeded";
  }
  return "<unknown>";
}

Plan prepare(const RenderQueue &queue, uint32_t producerKey, const terrain_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.status == terrain_recipe::Status::ValidEmpty) {
    return plan;
  }
  if (recipe.status != terrain_recipe::Status::Ready || recipe.faces.empty()) {
    plan.status = Status::InvalidRecipe;
    return plan;
  }
  plan.admission = painter_submission::preflight(
      queue, producerKey, recipe.faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  // Terrain paints as one span of consecutive order keys. If the queue already holds work that this
  // span would have to be ordered against, the renderer's order bias has to be able to tell the
  // span's last key apart; when it cannot, the picture would depend on submission order.
  const uint32_t baseSequence = queue.consumed ? 0u : queue.seq;
  if (recipe.faces.size() - 1u > UINT32_MAX - baseSequence) {
    plan.status = Status::InvalidOrder;
    return plan;
  }
  const uint32_t finalSequence = baseSequence + (uint32_t)recipe.faces.size() - 1u;
  if ((plan.admission.queued || plan.admission.existingObjects) &&
      !gpu_vk_order_bias_distinguishes(finalSequence)) {
    plan.status = Status::InvalidOrder;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const terrain_recipe::Recipe &recipe,
            const Plan &plan,
            const psxport::native_projection::ProjectionParams &projection) {
  if (plan.status != Status::Ready || recipe.status != terrain_recipe::Status::Ready) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight =
      gpu_vk_wide_engine(core) ? (gpu.s_da_x0 + gpu_vk_wide_engine_w(core) - 1) : gpu.s_da_x1;
  ProjectionPlaneScope plane(core->rsub.projParams, projection.h);
  RenderQueue::PainterObjectScope painter(queue, producerKey);
  for (size_t faceIndex = 0; faceIndex < recipe.faces.size(); ++faceIndex) {
    const auto &face = recipe.faces[faceIndex];
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (size_t i = 0; i < face.vertices.size(); ++i) {
      xs[i] = face.vertices[i].sx + gpu.s_off_x;
      ys[i] = face.vertices[i].sy + gpu.s_off_y;
      screenX[i] = face.vertices[i].screenX + (float)gpu.s_off_x;
      screenY[i] = face.vertices[i].screenY + (float)gpu.s_off_y;
      red[i] = (uint8_t)face.rgb[i];
      green[i] = (uint8_t)(face.rgb[i] >> 8);
      blue[i] = (uint8_t)(face.rgb[i] >> 16);
      depth[i] = core->rsub.projParams.pzToOrd(face.vertices[i].viewZ);
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      3,
                      0,
                      0,
                      xs,
                      ys,
                      screenX,
                      screenY,
                      us,
                      vs,
                      red,
                      green,
                      blue,
                      depth,
                      3,
                      0,
                      0,
                      0,
                      0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      0,
                      nullptr,
                      -1,
                      0.0f,
                      face.gouraud ? 1 : 0,
                      gpu.s_tp_dither ? 1 : 0,
                      scene_painter_order::cyclorama((uint32_t)faceIndex));
  }
  uint32_t grouped = 0;
  for (int i = 0; i < queue.n; ++i) {
    if (queue.items[i].painter_object == producerKey) {
      ++grouped;
    }
  }
  if (grouped != recipe.faces.size()) {
    lucent::error("terraindirect",
                  "FATAL grouped={}/{} under producer 0x{:08X}",
                  grouped,
                  recipe.faces.size(),
                  producerKey);
    std::abort();
  }
}

} // namespace spyro::terrain_submitter
