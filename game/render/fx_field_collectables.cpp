#include "fx_field_collectables.h"

#include "core.h"
#include "field_collectables_recipe.h"
#include "game.h"
#include "producer_scope.h"
#include "render_queue.h"

#include <cstdint>
#include <lucent/log.h>

namespace {

using spyro::field_collectables_recipe::Recipe;
using spyro::field_collectables_recipe::State;
using spyro::field_collectables_recipe::Status;

constexpr uint32_t kProducerKey = 0x80019300u;
constexpr uint32_t kFlightLevel = 0x80075690u;
constexpr uint32_t kHud = 0x80077fa8u;
constexpr uint32_t kHudSprites = kHud + 0x464u;
constexpr uint32_t kHudTiles = kHud + 0x564u;
constexpr uint32_t kShadedMobyQueue = 0x800720f4u;
constexpr uint32_t kShadedMobyCapacity = 256u;
constexpr uint32_t kSpecularTime = 0x800770f4u;
constexpr uint32_t kCosine = 0x8006cc78u;

State readState(Core *core) {
  State state{};
  state.flightLevel = core->mem_r32(kFlightLevel) != 0u;
  state.gemDisplay = core->mem_r8(kHud + 0u);
  state.dragonDisplay = core->mem_r8(kHud + 1u);
  state.lifeDisplay = core->mem_r8(kHud + 2u);
  state.eggDisplay = core->mem_r8(kHud + 3u);
  state.keyDisplay = core->mem_r8(kHud + 4u);
  state.keyFlag = core->mem_r32(kHud + 0x30u);
  state.lifeOrbCount = (int32_t)core->mem_r32(kHud + 0x38u);
  state.eggCount = (int32_t)core->mem_r32(kHud + 0x2cu);
  state.eggPhase = core->mem_r32(kHud + 0x40u);
  state.specularTime = core->mem_r32(kSpecularTime);
  for (uint32_t i = 0; i < state.rects.size(); ++i) {
    const uint32_t p = kHudSprites + i * 8u;
    state.rects[i] = {.x = (int16_t)core->mem_r16s(p + 0u),
                      .y = (int16_t)core->mem_r16s(p + 2u),
                      .w = (int16_t)core->mem_r16s(p + 4u),
                      .h = (int16_t)core->mem_r16s(p + 6u)};
  }
  for (uint32_t i = 0; i < state.tiles.size(); ++i) {
    const uint32_t p = kHudTiles + i * 8u;
    state.tiles[i] = {.u = core->mem_r8(p + 0u),
                      .v = core->mem_r8(p + 1u),
                      .clut = core->mem_r16(p + 2u),
                      .tpage = core->mem_r16(p + 6u)};
  }
  for (uint32_t i = 0; i < state.cosine.size(); ++i) {
    state.cosine[i] = core->mem_r16s(kCosine + i * 2u);
  }
  return state;
}

bool preflight(Core *core, const Recipe &recipe, uint32_t &queueEnd) {
  if (recipe.status != Status::Ready) {
    return false;
  }
  queueEnd = 0;
  while (queueEnd < kShadedMobyCapacity && core->mem_r32(kShadedMobyQueue + queueEnd * 4u)) {
    ++queueEnd;
  }
  if (recipe.shadedCount >= kShadedMobyCapacity - queueEnd) {
    return false;
  }
  const RenderQueue &queue = core->game->rq;
  const uint32_t queued = queue.consumed ? 0u : (uint32_t)queue.n;
  if (recipe.spriteCount > RQ_MAX - queued) {
    return false;
  }
  for (uint32_t i = 0; i < recipe.spriteCount; ++i) {
    const auto &sprite = recipe.sprites[i];
    if (sprite.rect.w <= 0 || sprite.rect.h <= 0 || ((sprite.tile.tpage >> 7u) & 3u) > 2u) {
      return false;
    }
  }
  return true;
}

void emitSprite(Core *core, const spyro::field_collectables_recipe::Sprite &sprite) {
  const GpuState &gpu = core->game->gpu;
  const int x0 = sprite.rect.x + gpu.s_off_x;
  const int y0 = sprite.rect.y + gpu.s_off_y;
  const int x1 = x0 + sprite.rect.w, y1 = y0 + sprite.rect.h;
  const int xs[4] = {x0, x1, x0, x1};
  const int ys[4] = {y0, y0, y1, y1};
  const int u0 = sprite.tile.u, v0 = sprite.tile.v;
  const int u1 = (uint8_t)(sprite.tile.u + sprite.rect.w);
  const int v1 = (uint8_t)(sprite.tile.v + sprite.rect.h);
  const int us[4] = {u0, u1, u0, u1};
  const int vs[4] = {v0, v0, v1, v1};
  const unsigned char rs[4] = {sprite.r, sprite.r, sprite.r, sprite.r};
  const unsigned char gs[4] = {sprite.g, sprite.g, sprite.g, sprite.g};
  const unsigned char bs[4] = {sprite.b, sprite.b, sprite.b, sprite.b};
  core->game->rq.push2dQuad(RQ_HUD,
                            1,
                            xs,
                            ys,
                            us,
                            vs,
                            rs,
                            gs,
                            bs,
                            (int)(sprite.tile.tpage & 0x0fu) * 64,
                            (int)((sprite.tile.tpage >> 4u) & 1u) * 256,
                            (int)((sprite.tile.tpage >> 7u) & 3u),
                            0,
                            (int)(sprite.tile.clut & 0x3fu) * 16,
                            (int)((sprite.tile.clut >> 6u) & 0x1ffu),
                            gpu.s_tw_mx,
                            gpu.s_tw_my,
                            gpu.s_tw_ox,
                            gpu.s_tw_oy,
                            gpu.s_da_x0,
                            gpu.s_da_y0,
                            gpu.s_da_x1,
                            gpu.s_da_y1,
                            0);
}

} // namespace

bool spyro_field_collectables_submit(Core *core) {
  const Recipe recipe = spyro::field_collectables_recipe::derive(readState(core));
  uint32_t queueEnd = 0;
  if (!preflight(core, recipe, queueEnd)) {
    lucent::debug("fieldhud",
                  "REFUSED status={} shaded={} sprites={}",
                  (uint32_t)recipe.status,
                  recipe.shadedCount,
                  recipe.spriteCount);
    return false;
  }
  for (uint32_t i = 0; i < recipe.shadedCount; ++i) {
    core->mem_w32(kShadedMobyQueue + (queueEnd + i) * 4u, recipe.shadedMobys[i]);
  }
  core->mem_w32(kShadedMobyQueue + (queueEnd + recipe.shadedCount) * 4u, 0u);
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "fieldhud:collectables");
  for (uint32_t i = 0; i < recipe.spriteCount; ++i) {
    emitSprite(core, recipe.sprites[i]);
  }
  return true;
}
