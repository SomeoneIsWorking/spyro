#include "moby_shadow_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "moby_shadow_recipe.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::moby_shadow_submitter {
namespace {

constexpr std::uint32_t kProducerKey = 0x80059F8Cu;
constexpr std::uint32_t kMobyShadows = 0x80075EF8u;
// Retail's third UV word is the first plus 0x1F00, so the closing corner shares u0 and sits 0x1F
// texels below v0. The addition cannot carry into the CLUT half, which is why it stays a UV edit.
constexpr std::uint8_t kThirdVertexVStep = 0x1Fu;

} // namespace

Plan prepare(Core *core, const RenderQueue &queue, std::size_t faceCount) {
  Plan plan{};
  if (core == nullptr || faceCount == 0u) {
    return plan;
  }
  const std::uint32_t first = core->mem_r32(kMobyShadows);
  const std::uint32_t second = core->mem_r32(kMobyShadows + 4u);
  const std::uint16_t clut = (std::uint16_t)(first >> 16);
  const std::uint16_t tpage = (std::uint16_t)(second >> 16);
  plan.mode = (int)((tpage >> 7) & 3u);
  if (plan.mode == 3) {
    // A colour mode of 3 is the queue's untextured sentinel. The shadow tile is always textured, so
    // this means the loader has not published one yet and the producer must refuse rather than
    // paint four flat triangles under every Moby.
    plan.status = Status::InvalidMaterial;
    return plan;
  }
  plan.tpX = (int)(tpage & 0x0fu) * 64;
  plan.tpY = (int)((tpage >> 4) & 1u) * 256;
  plan.clutX = (int)(clut & 0x3fu) * 16;
  plan.clutY = (int)((clut >> 6) & 0x1ffu);
  plan.blend = (int)((tpage >> 5) & 3u);
  plan.dither = (int)((tpage >> 9) & 1u);
  plan.u0 = (std::uint8_t)first;
  plan.v0 = (std::uint8_t)(first >> 8);
  plan.u1 = (std::uint8_t)second;
  plan.v1 = (std::uint8_t)(second >> 8);

  plan.admission = painter_submission::preflight(
      queue, kProducerKey, faceCount, scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            const moby_shadow_recipe::Recipe &recipe,
            const Plan &plan) {
  if (core == nullptr || core->game == nullptr || plan.status != Status::Ready ||
      recipe.status != moby_shadow_recipe::Status::Ready ||
      plan.admission.queued + (int)recipe.faces.size() > RQ_MAX) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight = std::max(
      gpu.s_da_x1, gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) - 1 : gpu.s_da_x1);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "mobyshadow");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  std::uint32_t shadowOrdinal = 0;
  std::uint32_t previousMoby = 0;
  for (const auto &face : recipe.faces) {
    if (face.moby != previousMoby) {
      // Ordinals count shadows, not faces, so one Moby's four triangles share a link position the
      // way retail's four packets share one allocation run.
      shadowOrdinal = previousMoby == 0u ? 0u : shadowOrdinal + 1u;
      previousMoby = face.moby;
    }
    core->rsub.diag.beginObject(face.moby);
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    const std::uint8_t uvU[3] = {plan.u0, plan.u1, plan.u0};
    const std::uint8_t uvV[3] = {plan.v0, plan.v1, (std::uint8_t)(plan.v0 + kThirdVertexVStep)};
    for (std::size_t v = 0; v < 3u; ++v) {
      const auto &vertex = face.vertices[v];
      xs[v] = vertex.sx + gpu.s_off_x;
      ys[v] = vertex.sy + gpu.s_off_y;
      screenX[v] = vertex.screenX + (float)gpu.s_off_x;
      screenY[v] = vertex.screenY + (float)gpu.s_off_y;
      depth[v] = core->rsub.projParams.pzToOrd(vertex.viewZ);
      us[v] = uvU[v];
      vs[v] = uvV[v];
      red[v] = face.grey;
      green[v] = face.grey;
      blue[v] = face.grey;
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      3,
                      1,
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
                      plan.mode,
                      plan.tpX,
                      plan.tpY,
                      plan.clutX,
                      plan.clutY,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      plan.blend,
                      nullptr,
                      -1,
                      0.0f,
                      0,
                      plan.dither,
                      scene_painter_order::mobyShadow(face.otBin, shadowOrdinal, face.fanOrdinal));
    core->rsub.diag.endObject();
  }
}

} // namespace spyro::moby_shadow_submitter
