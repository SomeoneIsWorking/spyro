#include "cyclorama_mask_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::cyclorama_mask_submitter {

Plan prepare(const Core *core, const RenderQueue &queue, std::span<const Draw> draws) {
  Plan plan{};
  if (core == nullptr || core->game == nullptr) {
    plan.status = Status::InvalidCore;
    return plan;
  }
  for (size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
    const Draw &draw = draws[drawIndex];
    if (draw.recipe == nullptr) {
      plan.status = Status::InvalidDraw;
      plan.faces.clear();
      return plan;
    }
    if (draw.recipe->status == cyclorama_mask_recipe::Status::ValidEmpty) {
      continue;
    }
    if (draw.recipe->status != cyclorama_mask_recipe::Status::Ready || draw.recipe->faces.empty()) {
      plan.status = Status::InvalidRecipe;
      plan.faces.clear();
      return plan;
    }
    for (size_t faceIndex = 0; faceIndex < draw.recipe->faces.size(); ++faceIndex) {
      const auto replay = scene_painter_order::cycloramaMask(
          draw.recipe->otBin, draw.recipe->portalOrdinal, (uint32_t)faceIndex);
      if (!replay.authored()) {
        plan.status = Status::InvalidOrder;
        plan.faces.clear();
        return plan;
      }
      plan.faces.push_back({drawIndex, faceIndex, replay});
    }
  }
  if (plan.faces.empty()) {
    return plan;
  }
  plan.admission = painter_submission::preflight(queue,
                                                 cyclorama_mask_recipe::kProducerKey,
                                                 plan.faces.size(),
                                                 scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    plan.faces.clear();
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core, RenderQueue &queue, std::span<const Draw> draws, const Plan &plan) {
  if (plan.status != Status::Ready || core == nullptr || core->game == nullptr) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(core)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(core) - 1);
  }
  ProducerScope producer(
      &core->rsub.producerScope, cyclorama_mask_recipe::kProducerKey, "cyclorama:mask");
  RenderQueue::PainterObjectScope painter(queue, cyclorama_mask_recipe::kProducerKey);
  for (const FaceRef &ref : plan.faces) {
    if (ref.draw >= draws.size() || draws[ref.draw].recipe == nullptr ||
        ref.face >= draws[ref.draw].recipe->faces.size()) {
      return;
    }
    const auto &face = draws[ref.draw].recipe->faces[ref.face];
    int xs[3]{}, ys[3]{}, us[3]{}, vs[3]{};
    float screenX[3]{}, screenY[3]{}, depth[3]{};
    unsigned char red[3]{}, green[3]{}, blue[3]{};
    for (size_t i = 0; i < face.vertices.size(); ++i) {
      xs[i] = face.vertices[i].sx + gpu.s_off_x;
      ys[i] = face.vertices[i].sy + gpu.s_off_y;
      screenX[i] = face.vertices[i].screenX + (float)gpu.s_off_x;
      screenY[i] = face.vertices[i].screenY + (float)gpu.s_off_y;
      depth[i] = 0.0f;
      red[i] = (uint8_t)face.rgb;
      green[i] = (uint8_t)(face.rgb >> 8);
      blue[i] = (uint8_t)(face.rgb >> 16);
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
                      0,
                      gpu.s_tp_dither,
                      ref.replay);
  }
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::InvalidDraw:
    return "invalid draw";
  case Status::InvalidRecipe:
    return "invalid recipe";
  case Status::InvalidOrder:
    return "invalid order";
  case Status::QueueCapacityExceeded:
    return "queue capacity exceeded";
  }
  return "unknown";
}

} // namespace spyro::cyclorama_mask_submitter
