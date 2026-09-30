#include "field_shadow_submitter.h"

#include "core.h"
#include "field_shadow_recipe.h"
#include "game.h"
#include "gpu_vk.h"
#include "margin_object_census.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "spyro_context.h"
#include "wide_screen_space.h"

#include <algorithm>

namespace spyro::field_shadow_submitter {
namespace {

constexpr std::uint32_t kProducerKey = 0x80059A48u;

} // namespace

Plan prepare(const RenderQueue &queue, std::size_t faceCount) {
  Plan plan{};
  if (faceCount == 0u) {
    return plan;
  }
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
            const field_shadow_recipe::Recipe &recipe,
            const Plan &plan) {
  if (core == nullptr || core->game == nullptr || plan.status != Status::Ready ||
      recipe.status != field_shadow_recipe::Status::Ready ||
      plan.admission.queued + (int)recipe.faceCount > RQ_MAX) {
    return;
  }
  const GpuState gpu = core->game->gpu;
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "fieldshadow");
  RenderQueue::PainterObjectScope painter(queue, kProducerKey);
  for (std::size_t i = 0; i < recipe.faceCount; ++i) {
    const auto &face = recipe.faces[i];
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    float screenX[4]{}, screenY[4]{}, depth[4]{};
    unsigned char red[4] = {0x80u, 0u, 0u, 0u};
    unsigned char green[4] = {0x80u, 0u, 0u, 0u};
    unsigned char blue[4] = {0x60u, 0u, 0u, 0u};
    for (std::size_t v = 0; v < 3u; ++v) {
      const auto &vertex = face.vertices[v];
      xs[v] = vertex.sx + gpu.s_off_x;
      ys[v] = vertex.sy + gpu.s_off_y;
      screenX[v] = vertex.screenX + (float)gpu.s_off_x;
      screenY[v] = vertex.screenY + (float)gpu.s_off_y;
      depth[v] = core->rsub.projParams.pzToOrd(vertex.viewZ);
    }
    // Past the recipe, so this shadow face is drawn. Its span is the per-class answer for shadows,
    // and it is only reachable at all once the shaded pass's 0x1100 near bound stages the Moby —
    // which is the point issue 0154 had to establish against retail before widening could count it.
    spyro_context(*core).marginCensus.addVertices(margin_object_census::Class::kShadow, xs, xs + 3);
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
                      wide_screen_space::drawAreaRight(core, gpu.s_da_x1),
                      gpu.s_da_y1,
                      2,
                      nullptr,
                      -1,
                      0.0f,
                      1,
                      1,
                      scene_painter_order::spyroShadow(face.otBin, face.fanOrdinal));
  }
}

} // namespace spyro::field_shadow_submitter
