#include "fx_screen_border.h"

#include "core.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"
#include "screen_border_recipe.h"

#include <lucent/log.h>

namespace {

constexpr uint32_t kProducerKey = 0x80018F30u;
constexpr uint32_t kEnabled = 0x8007570Cu;   // g_ScreenBorderEnabled
constexpr uint32_t kBarHeight = 0x800756C0u; // D_800756C0 — the animated bar height
constexpr uint32_t kDeltaTime = 0x800756CCu; // g_DeltaTime (main.c clamps it to 2..4)

} // namespace

bool spyro_screen_border_submit(Core *core,
                                int32_t drawOffsetX,
                                int32_t drawOffsetY,
                                int32_t renderWidth) {
  const uint32_t enabled = core->mem_r32(kEnabled);
  const int32_t height = static_cast<int32_t>(core->mem_r32(kBarHeight));
  if (enabled == 0u && height == 0) {
    return true; // the guest's own gate: the producer is not even called
  }
  const int32_t deltaTime = static_cast<int32_t>(core->mem_r32(kDeltaTime));
  const auto recipe = spyro::screen_border_recipe::field(
      enabled, height, deltaTime, drawOffsetX, drawOffsetY, renderWidth);
  core->mem_w32(kBarHeight, static_cast<uint32_t>(recipe.barHeight));
  if (!recipe.visible) {
    return true;
  }
  if (recipe.x0 >= recipe.x1 || recipe.topY0 >= recipe.topY1 ||
      recipe.bottomY0 >= recipe.bottomY1) {
    return false;
  }
  RenderQueue &queue = core->game->rq;
  const GpuState gpu = core->game->gpu;
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "screen:border");
  RenderQueue::Space2dScope wide(queue, RQ_2D_WIDE_FINAL);
  const int32_t barYs[2][4] = {
      {recipe.topY0, recipe.topY0, recipe.topY1, recipe.topY1},
      {recipe.bottomY0, recipe.bottomY0, recipe.bottomY1, recipe.bottomY1}};
  for (const auto *ys : barYs) {
    const int xs[4] = {recipe.x0, recipe.x1, recipe.x0, recipe.x1};
    const int us[4] = {};
    const int vs[4] = {};
    const unsigned char black[4] = {0, 0, 0, 0};
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
                      black,
                      black,
                      black,
                      nullptr,
                      0,
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
                      0);
  }
  return true;
}
