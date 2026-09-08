#include "field_shaded_queue_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::field_shaded_queue_submitter {

Plan prepare(const RenderQueue &queue,
             uint32_t producerKey,
             const field_shaded_queue_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.status == field_shaded_queue_recipe::Status::ValidEmpty) {
    return plan;
  }
  if (recipe.status != field_shaded_queue_recipe::Status::Ready || recipe.faces.empty()) {
    plan.status = Status::InvalidRecipe;
    return plan;
  }
  for (const auto &face : recipe.faces) {
    if ((face.vertexCount != 3u && face.vertexCount != 4u) ||
        !scene_painter_order::queuedWorld(face.otBin, face.paintGroup).authored()) {
      plan.status = Status::InvalidOrder;
      return plan;
    }
  }
  plan.admission = painter_submission::preflight(
      queue, producerKey, recipe.faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const field_shaded_queue_recipe::Recipe &recipe,
            const Plan &plan) {
  if (plan.status != Status::Ready || recipe.status != field_shaded_queue_recipe::Status::Ready) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(core)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(core) - 1);
  }
  RenderQueue::PainterObjectScope painter(queue, producerKey);
  for (const auto &face : recipe.faces) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < face.vertexCount; ++i) {
      xs[i] = face.vertices[i].sx + gpu.s_off_x;
      ys[i] = face.vertices[i].sy + gpu.s_off_y;
      screenX[i] = face.vertices[i].screenX + (float)gpu.s_off_x;
      screenY[i] = face.vertices[i].screenY + (float)gpu.s_off_y;
      depth[i] = core->rsub.projParams.pzToOrd(face.vertices[i].viewZ);
      red[i] = (uint8_t)face.rgb[i];
      green[i] = (uint8_t)(face.rgb[i] >> 8);
      blue[i] = (uint8_t)(face.rgb[i] >> 16);
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      face.vertexCount,
                      face.semiTransparent ? 1 : 0,
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
                      gpu.s_tp_dither,
                      scene_painter_order::queuedWorld(face.otBin, face.paintGroup));
  }
}

} // namespace spyro::field_shaded_queue_submitter
