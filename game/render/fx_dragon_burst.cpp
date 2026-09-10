#include "fx_dragon_burst.h"

#include "core.h"
#include "dragon_burst_recipe.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x80058864u;
// Retail heads the chain with a draw-mode packet of 0xE1000620: dithering on and semi-transparency
// mode 1, the additive blend. The triangles are the untextured GP0 0x22 command.
constexpr int kUntexturedMode = 3;
constexpr int kAdditiveBlend = 1;
constexpr int kDither = 1;
constexpr int kTriangleVertices = 3;

} // namespace

bool dragon_burst_submit(Core *core) {
  const auto recipe = spyro::dragon_burst::derive(core);
  if (recipe.status == spyro::dragon_burst::Status::Inactive) {
    lucent::debug("dragonburst", "inactive");
    return true;
  }
  if (recipe.status != spyro::dragon_burst::Status::Ready) {
    lucent::debug(
        "dragonburst", "REFUSED recipe={}", spyro::dragon_burst::statusName(recipe.status));
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const GpuState gpu = core->game->gpu;
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "dragon:burst");
  RenderQueue::Space2dScope wide(queue, RQ_2D_WIDE_FINAL);
  for (const auto &triangle : recipe.triangles) {
    int xs[4]{}, ys[4]{}, us[4]{}, vs[4]{};
    unsigned char red[4]{}, green[4]{}, blue[4]{};
    for (size_t v = 0; v < triangle.vertices.size(); ++v) {
      xs[v] = triangle.vertices[v].sx + gpu.s_off_x;
      ys[v] = triangle.vertices[v].sy + gpu.s_off_y;
      // A flat GP0 0x22 carries one colour word for the whole primitive, and this producer writes
      // the same byte into all three channels.
      red[v] = recipe.colour;
      green[v] = recipe.colour;
      blue[v] = recipe.colour;
    }
    queue.emitOrQueue(core,
                      1,
                      RQ_HUD,
                      RQ_OM_2D_FG,
                      kTriangleVertices,
                      1,
                      0,
                      xs,
                      ys,
                      nullptr,
                      nullptr,
                      us,
                      vs,
                      red,
                      green,
                      blue,
                      nullptr,
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
                      gpu.s_da_x1,
                      gpu.s_da_y1,
                      kAdditiveBlend,
                      nullptr,
                      -1,
                      0.0f,
                      0,
                      kDither);
  }
  lucent::debug("dragonburst", "triangles={} colour={}", recipe.triangles.size(), recipe.colour);
  return true;
}
