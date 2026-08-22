#include "fx_world_draw.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "world_recipe.h"
#include "world_scene_builder.h"

#include <algorithm>
#include <cstdint>
#include <lucent/log.h>
#include <vector>

namespace {

constexpr uint32_t kProducerKey = 0x800258f0u;
constexpr uint32_t kBroadVisibility = 0x800771c8u;

uint32_t vertexCount(spyro::world_recipe::Family family) {
  using spyro::world_recipe::Family;
  return family == Family::G4 || family == Family::GT4 ? 4u : 3u;
}

bool validate(const spyro::world_recipe::Recipe &recipe, std::vector<size_t> &paintOrder) {
  if (!spyro::world_recipe::paintOrder(recipe.faces, paintOrder)) {
    return false;
  }
  for (const auto &face : recipe.faces) {
    const uint32_t count = vertexCount(face.family);
    if (face.vertexCount != count) {
      return false;
    }
    const bool textured = face.family == spyro::world_recipe::Family::GT3 ||
                          face.family == spyro::world_recipe::Family::GT4;
    if (textured != face.material.textured ||
        (textured && ((face.material.tpage >> 7) & 3u) > 2u) ||
        !spyro::scene_painter_order::world(face.otBin, face.paintGroup, face.paintSuborder)
             .authored()) {
      return false;
    }
  }
  return true;
}

} // namespace

bool spyro_world_submit(Core *core, int32_t selection) {
  if (!core || !core->game) {
    return false;
  }
  const spyro::world_recipe::Recipe recipe = spyro::world_scene::build(core, selection);
  if (recipe.status == spyro::world_recipe::Status::ValidEmpty) {
    for (uint32_t i = 0; i < recipe.broadVisible.size(); ++i) {
      core->mem_w8(kBroadVisibility + i, recipe.broadVisible[i]);
    }
    return true;
  }
  if (recipe.status != spyro::world_recipe::Status::Ready) {
    lucent::debug(
        "worlddirect",
        "REFUSED status={} reason={} selected={} low={} high={} candidates={} rejected={}",
        (uint32_t)recipe.status,
        recipe.refusal,
        recipe.selectedSectors,
        recipe.lowSectors,
        recipe.highSectors,
        recipe.candidates,
        recipe.rejected);
    return false;
  }

  std::vector<size_t> paintOrder;
  if (!validate(recipe, paintOrder)) {
    lucent::debug("worlddirect", "REFUSED invalid final paint/material recipe");
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const PainterObjectAdmission admission = queue.preflightPainterObject(
      kProducerKey, recipe.faces.size(), spyro::scene_painter_order::kStage13Domain);
  if (!admission.accepted()) {
    return false;
  }
  const uint32_t baseSequence = queue.consumed ? 0u : queue.seq;
  if (recipe.faces.size() - 1u > UINT32_MAX - baseSequence) {
    return false;
  }
  const uint32_t finalSequence = baseSequence + (uint32_t)recipe.faces.size() - 1u;
  if ((admission.queued_items || admission.existing_objects) &&
      !gpu_vk_order_bias_distinguishes(finalSequence)) {
    return false;
  }
  const GpuState gpu = core->game->gpu;
  if (gpu.s_da_x0 > gpu.s_da_x1 || gpu.s_da_y0 > gpu.s_da_y1) {
    return false;
  }
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(core)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(core) - 1);
  }

  // The builder and all capacity/material/order checks above are read-only.
  // Publish the guest-visible cull result only after the complete scene has
  // been accepted, immediately before the infallible preflighted queue writes.
  for (uint32_t i = 0; i < recipe.broadVisible.size(); ++i) {
    core->mem_w8(kBroadVisibility + i, recipe.broadVisible[i]);
  }

  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "world:static");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (size_t faceIndex : paintOrder) {
    const auto &face = recipe.faces[faceIndex];
    const uint32_t count = vertexCount(face.family);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < count; ++i) {
      const auto &vertex = face.vertices[i];
      xs[i] = vertex.sx + gpu.s_off_x;
      ys[i] = vertex.sy + gpu.s_off_y;
      screenX[i] = vertex.screenX + (float)gpu.s_off_x;
      screenY[i] = vertex.screenY + (float)gpu.s_off_y;
      us[i] = vertex.u;
      vs[i] = vertex.v;
      red[i] = (uint8_t)vertex.rgb;
      green[i] = (uint8_t)(vertex.rgb >> 8);
      blue[i] = (uint8_t)(vertex.rgb >> 16);
      depth[i] = core->rsub.projParams.pzToOrd(vertex.viewZ);
    }
    const bool textured = face.material.textured;
    queue.emitOrQueue(
        core,
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
        gpu.s_tw_mx,
        gpu.s_tw_my,
        gpu.s_tw_ox,
        gpu.s_tw_oy,
        gpu.s_da_x0,
        gpu.s_da_y0,
        drawRight,
        gpu.s_da_y1,
        (face.material.tpage >> 5) & 3u,
        nullptr,
        -1,
        0.0f,
        1,
        textured ? (face.material.tpage >> 9) & 1u : gpu.s_tp_dither,
        spyro::scene_painter_order::world(face.otBin, face.paintGroup, face.paintSuborder));
  }
  lucent::debug("worlddirect",
                "PASS selected={} low={} high={} candidates={} rejected={} faces={} "
                "painters_before={}",
                recipe.selectedSectors,
                recipe.lowSectors,
                recipe.highSectors,
                recipe.candidates,
                recipe.rejected,
                recipe.faces.size(),
                admission.existing_objects);
  return true;
}
