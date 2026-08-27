#include "fx_screen_fade.h"

#include "core.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

namespace {

constexpr uint32_t kProducerKey = 0x800190D4u;

} // namespace

bool spyro_screen_fade_submit(Core *core, const spyro::screen_fade_recipe::Recipe &recipe) {
  if (!recipe.visible) {
    return true;
  }
  if (recipe.x0 >= recipe.x1 || recipe.y0 >= recipe.y1 || recipe.blendMode > 3u) {
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const GpuState gpu = core->game->gpu;
  const int xs[4] = {recipe.x0, recipe.x1, recipe.x0, recipe.x1};
  const int ys[4] = {recipe.y0, recipe.y0, recipe.y1, recipe.y1};
  const int us[4] = {};
  const int vs[4] = {};
  const unsigned char rs[4] = {recipe.r, recipe.r, recipe.r, recipe.r};
  const unsigned char gs[4] = {recipe.g, recipe.g, recipe.g, recipe.g};
  const unsigned char bs[4] = {recipe.b, recipe.b, recipe.b, recipe.b};
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "screen:fade");
  RenderQueue::Space2dScope wide(queue, RQ_2D_WIDE_FINAL);
  queue.emitOrQueue(core,
                    1,
                    RQ_HUD,
                    RQ_OM_2D_FG,
                    4,
                    1,
                    0,
                    xs,
                    ys,
                    nullptr,
                    nullptr,
                    us,
                    vs,
                    rs,
                    gs,
                    bs,
                    nullptr,
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
                    recipe.x1 - 1,
                    gpu.s_da_y1,
                    recipe.blendMode);
  return true;
}
