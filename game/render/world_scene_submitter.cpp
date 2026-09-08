#include "world_scene_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::world_scene_submitter {
namespace {

constexpr uint32_t kBroadVisibility = 0x800771c8u;

uint32_t vertexCount(world_recipe::Family family) {
  using world_recipe::Family;
  return family == Family::G4 || family == Family::GT4 ? 4u : 3u;
}

bool validFaces(const world_recipe::Recipe &recipe, std::vector<size_t> &paintOrder) {
  if (!world_recipe::paintOrder(recipe.faces, paintOrder)) {
    return false;
  }
  for (const auto &face : recipe.faces) {
    const uint32_t count = vertexCount(face.family);
    const bool textured =
        face.family == world_recipe::Family::GT3 || face.family == world_recipe::Family::GT4;
    if (face.vertexCount != count || textured != face.material.textured ||
        (textured && ((face.material.tpage >> 7) & 3u) > 2u) ||
        !scene_painter_order::world(face.otBin, face.paintGroup, face.paintSuborder).authored()) {
      return false;
    }
  }
  return true;
}

bool validSubmission(const Core *core, const world_recipe::Recipe &recipe, const Plan &plan) {
  return core != nullptr && core->game != nullptr &&
         ((plan.status == Status::Ready && recipe.status == world_recipe::Status::Ready &&
           plan.paintOrder.size() == recipe.faces.size()) ||
          (plan.status == Status::ValidEmpty && recipe.status == world_recipe::Status::ValidEmpty));
}

} // namespace

Plan prepare(const DrawState &draw,
             const RenderQueue &queue,
             uint32_t producerKey,
             const world_recipe::Recipe &recipe) {
  Plan plan{};
  plan.draw = draw;
  if (draw.areaLeft > draw.areaRight || draw.areaTop > draw.areaBottom) {
    plan.status = Status::InvalidDrawArea;
    return plan;
  }
  if (recipe.status == world_recipe::Status::ValidEmpty) {
    return plan;
  }
  if (recipe.status != world_recipe::Status::Ready || recipe.faces.empty()) {
    plan.status = Status::InvalidRecipe;
    return plan;
  }
  if (!validFaces(recipe, plan.paintOrder)) {
    plan.status = Status::InvalidOrder;
    plan.paintOrder.clear();
    return plan;
  }
  plan.admission = painter_submission::preflight(
      queue, producerKey, recipe.faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    plan.paintOrder.clear();
    return plan;
  }
  const uint32_t baseSequence = queue.consumed ? 0u : queue.seq;
  if (recipe.faces.size() - 1u > UINT32_MAX - baseSequence ||
      ((plan.admission.queued || plan.admission.existingObjects) &&
       !gpu_vk_order_bias_distinguishes(baseSequence + (uint32_t)recipe.faces.size() - 1u))) {
    plan.status = Status::OrderPrecisionExceeded;
    plan.paintOrder.clear();
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

std::optional<DrawState> captureDrawState(Core &core) {
  if (!core.game) {
    return std::nullopt;
  }
  const GpuState &gpu = core.game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return std::nullopt;
  }
  return DrawState{.offsetX = gpu.s_off_x,
                   .offsetY = gpu.s_off_y,
                   .areaLeft = gpu.s_da_x0,
                   .areaTop = gpu.s_da_y0,
                   .areaRight = gpu_vk_wide_engine(&core)
                                    ? std::max(gpu.s_da_x1, gpu_vk_wide_engine_w(&core) - 1)
                                    : gpu.s_da_x1,
                   .areaBottom = gpu.s_da_y1,
                   .windowMaskX = gpu.s_tw_mx,
                   .windowMaskY = gpu.s_tw_my,
                   .windowOffsetX = gpu.s_tw_ox,
                   .windowOffsetY = gpu.s_tw_oy,
                   .dither = gpu.s_tp_dither,
                   .projectionH = core.rsub.projParams.projH()};
}

Plan prepare(Core *core,
             const RenderQueue &queue,
             uint32_t producerKey,
             const world_recipe::Recipe &recipe) {
  if (!core || !core->game) {
    Plan out{};
    out.status = Status::InvalidRecipe;
    return out;
  }
  const auto draw = captureDrawState(*core);
  if (!draw) {
    Plan out{};
    out.status = Status::InvalidDrawArea;
    return out;
  }
  return prepare(*draw, queue, producerKey, recipe);
}

namespace {

void emitPrepared(Core *core,
                  RenderQueue &queue,
                  uint32_t producerKey,
                  const world_recipe::Recipe &recipe,
                  const Plan &plan) {
  if (plan.status == Status::ValidEmpty) {
    return;
  }

  const DrawState &draw = plan.draw;
  ProjParams depthProjection;
  depthProjection.setProjH(draw.projectionH);
  RenderQueue::PainterObjectScope painter(queue, producerKey);
  for (size_t faceIndex : plan.paintOrder) {
    const auto &face = recipe.faces[faceIndex];
    const uint32_t count = vertexCount(face.family);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < count; ++i) {
      const auto &vertex = face.vertices[i];
      xs[i] = vertex.sx + draw.offsetX;
      ys[i] = vertex.sy + draw.offsetY;
      screenX[i] = vertex.screenX + (float)draw.offsetX;
      screenY[i] = vertex.screenY + (float)draw.offsetY;
      us[i] = vertex.u;
      vs[i] = vertex.v;
      red[i] = (uint8_t)vertex.rgb;
      green[i] = (uint8_t)(vertex.rgb >> 8);
      blue[i] = (uint8_t)(vertex.rgb >> 16);
      depth[i] = depthProjection.pzToOrd(vertex.viewZ);
    }
    const bool textured = face.material.textured;
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      (int)count,
                      face.material.semiTransparent ? 1 : 0,
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
                      textured ? (face.material.tpage >> 7) & 3u : 3u,
                      textured ? (face.material.tpage & 0x0fu) * 64 : 0,
                      textured ? ((face.material.tpage >> 4) & 1u) * 256 : 0,
                      textured ? (face.material.clut & 0x3fu) * 16 : 0,
                      textured ? (face.material.clut >> 6) & 0x1ffu : 0,
                      draw.windowMaskX,
                      draw.windowMaskY,
                      draw.windowOffsetX,
                      draw.windowOffsetY,
                      draw.areaLeft,
                      draw.areaTop,
                      draw.areaRight,
                      draw.areaBottom,
                      (face.material.tpage >> 5) & 3u,
                      nullptr,
                      -1,
                      0.0f,
                      1,
                      textured ? (face.material.tpage >> 9) & 1u : draw.dither,
                      scene_painter_order::world(face.otBin, face.paintGroup, face.paintSuborder));
  }
}

} // namespace

void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            const world_recipe::Recipe &recipe,
            const Plan &plan) {
  if (!validSubmission(core, recipe, plan)) {
    return;
  }
  for (uint32_t i = 0; i < recipe.broadVisible.size(); ++i) {
    core->mem_w8(kBroadVisibility + i, recipe.broadVisible[i]);
  }
  emitPrepared(core, queue, producerKey, recipe, plan);
}

bool emit(Core *core,
          RenderQueue &queue,
          uint32_t producerKey,
          const world_recipe::Recipe &recipe,
          const Plan &plan) {
  if (!validSubmission(core, recipe, plan)) {
    return false;
  }
  emitPrepared(core, queue, producerKey, recipe, plan);
  return true;
}

} // namespace spyro::world_scene_submitter
