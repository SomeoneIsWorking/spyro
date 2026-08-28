#include "actor_face_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>
#include <utility>

namespace spyro::actor_face_submitter {
namespace {

uint32_t vertex_count(actor_draw_recipe::Family family) {
  using Family = actor_draw_recipe::Family;
  return family == Family::G4 || family == Family::GT4 ? 4u : 3u;
}

std::array<uint32_t, 4> vertex_order(const actor_draw_recipe::Face &face) {
  std::array<uint32_t, 4> order{0, 1, 2, 3};
  if (face.origin == actor_draw_recipe::Origin::QuadSecond) {
    order[0] = 3;
  }
  return order;
}

Material material_for(const actor_draw_recipe::Face &face) {
  using Family = actor_draw_recipe::Family;
  using Origin = actor_draw_recipe::Origin;
  Material material{};
  material.textured = face.family == Family::GT3 || face.family == Family::GT4;
  material.semiTransparent = (face.input.words[1] & 1u) != 0u;
  if (!material.textured) {
    return material;
  }
  const auto &input = face.input;
  const bool sourceQuad = (int32_t)input.words[0] < 0;
  if (face.family == Family::GT4) {
    material.attributes = {
        input.words[3] + input.fog, input.words[4], input.words[5], input.words[5] >> 16};
  } else if (!sourceQuad) {
    material.attributes = {input.words[2] + input.fog, input.words[3], input.words[4], 0};
  } else if (face.origin == Origin::QuadSecond) {
    material.attributes = {(input.words[3] + input.fog) & 0xffff0000u | (input.words[5] >> 16),
                           input.words[4],
                           input.words[5],
                           0};
  } else {
    material.attributes = {input.words[3] + input.fog, input.words[4], input.words[5], 0};
  }
  material.clut = (uint16_t)(material.attributes[0] >> 16);
  material.tpage = (uint16_t)(material.attributes[1] >> 16);
  return material;
}

} // namespace

Plan prepare(const RenderQueue &queue,
             uint32_t producerKey,
             std::span<const actor_prefix::Output> records,
             std::span<const actor_draw_recipe::Face> faces) {
  Plan plan{};
  if (faces.empty()) {
    return plan;
  }
  plan.materials.reserve(faces.size());
  for (const auto &face : faces) {
    Material material = material_for(face);
    if (material.textured && ((material.tpage >> 7) & 3u) > 2u) {
      plan.status = Status::UnsupportedMaterial;
      plan.materials.clear();
      return plan;
    }
    plan.materials.push_back(material);
  }
  auto replay = actor_global_order::build(records, faces);
  if (replay.status != actor_global_order::Status::Ready) {
    plan.status = Status::InvalidGlobalOrder;
    plan.materials.clear();
    return plan;
  }
  plan.replay = std::move(replay.faces);
  plan.admission = painter_submission::preflight(
      queue, producerKey, faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    plan.replay.clear();
    plan.materials.clear();
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            uint32_t producerKey,
            Layer layer,
            std::span<const actor_draw_recipe::Face> faces,
            const Plan &plan) {
  if (plan.status != Status::Ready || plan.replay.size() != faces.size() ||
      plan.materials.size() != faces.size()) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  int drawRight = gpu.s_da_x1;
  if (gpu_vk_wide_engine(core)) {
    drawRight = std::max(drawRight, gpu_vk_wide_engine_w(core) - 1);
  }
  RenderQueue::PainterObjectScope painter(queue, producerKey);
  for (const auto &replayFace : plan.replay) {
    const auto &face = faces[replayFace.faceIndex];
    const Material &material = plan.materials[replayFace.faceIndex];
    const uint32_t count = vertex_count(face.family);
    const auto order = vertex_order(face);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t source = order[i];
      xs[i] = (int16_t)face.input.xy[source] + gpu.s_off_x;
      ys[i] = (int16_t)(face.input.xy[source] >> 16) + gpu.s_off_y;
      screenX[i] = face.input.screenX[source] + (float)gpu.s_off_x;
      screenY[i] = face.input.screenY[source] + (float)gpu.s_off_y;
      us[i] = material.attributes[i] & 0xffu;
      vs[i] = (material.attributes[i] >> 8) & 0xffu;
      const uint32_t rgb = face.input.color[source];
      red[i] = rgb;
      green[i] = rgb >> 8;
      blue[i] = rgb >> 16;
      depth[i] = core->rsub.projParams.pzToOrd(face.input.viewZ[source]);
    }
    const PainterReplayOrder replayOrder =
        layer == Layer::Regular
            ? scene_painter_order::actor(
                  replayFace.otBin, replayFace.recordOrdinal, replayFace.chainOrdinal)
            : scene_painter_order::secondaryActor(
                  replayFace.otBin, replayFace.recordOrdinal, replayFace.chainOrdinal);
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      (int)count,
                      material.semiTransparent ? 1 : 0,
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
                      material.textured ? (material.tpage >> 7) & 3u : 3u,
                      material.textured ? (material.tpage & 0x0fu) * 64 : 0,
                      material.textured ? ((material.tpage >> 4) & 1u) * 256 : 0,
                      material.textured ? (material.clut & 0x3fu) * 16 : 0,
                      material.textured ? (material.clut >> 6) & 0x1ffu : 0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      material.textured ? (material.tpage >> 5) & 3u : 0,
                      nullptr,
                      -1,
                      0.0f,
                      1,
                      material.textured ? (material.tpage >> 9) & 1u : gpu.s_tp_dither,
                      replayOrder);
  }
}

} // namespace spyro::actor_face_submitter
