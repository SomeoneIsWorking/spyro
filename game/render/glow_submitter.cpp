#include "glow_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::glow_submitter {
namespace {

constexpr std::uint32_t kProducerKey = 0x800580F4u;
// Retail heads each record's chain with a draw-mode packet of 0xE1000220: dithering on and
// semi-transparency mode 1, the additive blend that makes a halo brighten what it covers. The
// triangles themselves are the untextured GP0 0x32 command, so the queue's colour mode is 3.
constexpr int kUntexturedMode = 3;
constexpr int kAdditiveBlend = 1;
constexpr int kDither = 1;

} // namespace

Plan prepare(const RenderQueue &queue, const glow_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.faces.empty()) {
    return plan;
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

void submit(Core *core, RenderQueue &queue, const glow_recipe::Recipe &recipe, const Plan &plan) {
  if (core == nullptr || core->game == nullptr || plan.status != Status::Ready ||
      recipe.status != glow_recipe::Status::Ready ||
      plan.admission.queued + (int)recipe.faces.size() > RQ_MAX) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight = std::max(
      gpu.s_da_x1, gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) - 1 : gpu.s_da_x1);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "glow");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (const auto &face : recipe.faces) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (std::size_t v = 0; v < face.vertices.size(); ++v) {
      const auto &vertex = face.vertices[v];
      xs[v] = vertex.sx + gpu.s_off_x;
      ys[v] = vertex.sy + gpu.s_off_y;
      screenX[v] = vertex.screenX + (float)gpu.s_off_x;
      screenY[v] = vertex.screenY + (float)gpu.s_off_y;
      depth[v] = core->rsub.projParams.pzToOrd(vertex.viewZ);
    }
    // Only the centre carries the record's colour. The ring is black, which is what makes the halo
    // fall off; writing the colour to all three would paint a flat triangle.
    red[0] = (unsigned char)(face.colour & 0xffu);
    green[0] = (unsigned char)((face.colour >> 8) & 0xffu);
    blue[0] = (unsigned char)((face.colour >> 16) & 0xffu);
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
                      kUntexturedMode,
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
                      kAdditiveBlend,
                      nullptr,
                      -1,
                      0.0f,
                      0,
                      kDither,
                      scene_painter_order::glow(face.otBin, face.recordIndex, face.fanOrdinal));
  }
}

} // namespace spyro::glow_submitter
