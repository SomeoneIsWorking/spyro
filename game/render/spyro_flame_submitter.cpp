#include "spyro_flame_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::flame_submitter {
namespace {

constexpr std::uint32_t kProducerKey = 0x80058D64u;

} // namespace

Plan prepare(const RenderQueue &queue, const flame_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.faces.empty()) {
    return plan;
  }
  for (const auto &face : recipe.faces) {
    // The ribbon's Tiledef is always textured, so a textured face whose colour mode reads 3 means
    // the flame's UV pair has not been published yet. The whole recipe is refused here rather than
    // dropped face by face during emission, because 3 is also the queue's untextured sentinel and a
    // painter object accepts no other value.
    if (face.textured && ((face.tpage >> 7) & 3u) == 3u) {
      plan.status = Status::InvalidMaterial;
      return plan;
    }
  }
  plan.admission = painter_submission::preflight(
      queue, kProducerKey, recipe.faces.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core, RenderQueue &queue, const flame_recipe::Recipe &recipe, const Plan &plan) {
  if (core == nullptr || core->game == nullptr || plan.status != Status::Ready ||
      recipe.status != flame_recipe::Status::Ready ||
      plan.admission.queued + (int)recipe.faces.size() > RQ_MAX) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight = std::max(
      gpu.s_da_x1, gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) - 1 : gpu.s_da_x1);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "spyroflame");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (const auto &face : recipe.faces) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (std::size_t v = 0; v < face.nv; ++v) {
      const auto &vertex = face.vertices[v];
      xs[v] = vertex.sx + gpu.s_off_x;
      ys[v] = vertex.sy + gpu.s_off_y;
      screenX[v] = vertex.screenX + (float)gpu.s_off_x;
      screenY[v] = vertex.screenY + (float)gpu.s_off_y;
      depth[v] = core->rsub.projParams.pzToOrd(vertex.viewZ);
      us[v] = face.u[v];
      vs[v] = face.v[v];
      red[v] = face.red[v];
      green[v] = face.green[v];
      blue[v] = face.blue[v];
    }
    // The tip fan is an untextured Gouraud triangle, which the queue names with colour mode 3.
    // A painter object refuses any other value, so a negative sentinel here would have the whole
    // frame rejected at the next producer's preflight rather than at this one.
    int mode = 3;
    int tpX = 0, tpY = 0, clutX = 0, clutY = 0, blend = 0, dither = 0;
    if (face.textured) {
      mode = (int)((face.tpage >> 7) & 3u);
      tpX = (int)(face.tpage & 0x0fu) * 64;
      tpY = (int)((face.tpage >> 4) & 1u) * 256;
      clutX = (int)(face.clut & 0x3fu) * 16;
      clutY = (int)((face.clut >> 6) & 0x1ffu);
      blend = (int)((face.tpage >> 5) & 3u);
      dither = (int)((face.tpage >> 9) & 1u);
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      (int)face.nv,
                      face.semi ? 1 : 0,
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
                      mode,
                      tpX,
                      tpY,
                      clutX,
                      clutY,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawRight,
                      gpu.s_da_y1,
                      blend,
                      nullptr,
                      -1,
                      0.0f,
                      0,
                      dither,
                      scene_painter_order::flame(face.otBin, face.part, face.faceOrdinal));
  }
}

} // namespace spyro::flame_submitter
