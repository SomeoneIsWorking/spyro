#include "sparkle_submitter.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"

#include <algorithm>

namespace spyro::sparkle_submitter {
namespace {

constexpr std::uint32_t kProducerKey = 0x800584C4u;
// Each stroke is the GP0 0x40 monochrome line command, which carries no texture word at all, so the
// queue's untextured colour mode is the only material a line can be admitted with.
constexpr int kUntexturedMode = 3;
constexpr int kOpaque = 0;
constexpr int kNoDither = 0;
constexpr int kLineVertices = 2;

} // namespace

Plan prepare(const RenderQueue &queue, const sparkle_recipe::Recipe &recipe) {
  Plan plan{};
  if (recipe.lines.empty()) {
    return plan;
  }
  plan.admission = painter_submission::preflight(
      queue, kProducerKey, recipe.lines.size(), scene_painter_order::kActorWorldTerrainDomain);
  if (!plan.admission.ready) {
    plan.status = Status::QueueCapacityExceeded;
    return plan;
  }
  plan.status = Status::Ready;
  return plan;
}

void submit(Core *core,
            RenderQueue &queue,
            const sparkle_recipe::Recipe &recipe,
            const Plan &plan) {
  if (core == nullptr || core->game == nullptr || plan.status != Status::Ready ||
      recipe.status != sparkle_recipe::Status::Ready ||
      plan.admission.queued + (int)recipe.lines.size() > RQ_MAX) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  const int drawRight = std::max(
      gpu.s_da_x1, gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) - 1 : gpu.s_da_x1);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "sparkle");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (const auto &line : recipe.lines) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (std::size_t v = 0; v < line.vertices.size(); ++v) {
      const auto &vertex = line.vertices[v];
      xs[v] = vertex.sx + gpu.s_off_x;
      ys[v] = vertex.sy + gpu.s_off_y;
      screenX[v] = vertex.screenX + (float)gpu.s_off_x;
      screenY[v] = vertex.screenY + (float)gpu.s_off_y;
      depth[v] = core->rsub.projParams.pzToOrd(vertex.viewZ);
      // A monochrome line carries one colour word for the whole primitive, so both ends take it.
      red[v] = (unsigned char)(line.colour & 0xffu);
      green[v] = (unsigned char)((line.colour >> 8) & 0xffu);
      blue[v] = (unsigned char)((line.colour >> 16) & 0xffu);
    }
    queue.emitOrQueue(
        core,
        1,
        RQ_WORLD,
        RQ_OM_DEPTH,
        kLineVertices,
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
        kOpaque,
        nullptr,
        -1,
        0.0f,
        0,
        kNoDither,
        scene_painter_order::sparkle(line.otBin, line.recordIndex, line.chainOrdinal));
  }
}

} // namespace spyro::sparkle_submitter
