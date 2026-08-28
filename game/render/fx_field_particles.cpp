#include "fx_field_particles.h"

#include "core.h"
#include "field_particles_recipe.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "render_queue.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <cstdint>
#include <lucent/log.h>
#include <span>

namespace {

constexpr uint32_t kProducerKey = 0x800573c8u;
constexpr uint32_t kCamera = 0x80076dd0u;

bool preflight(Core *core, const spyro::field_particles_recipe::Recipe &recipe) {
  if (recipe.status != spyro::field_particles_recipe::Status::Ready) {
    return recipe.status == spyro::field_particles_recipe::Status::ValidEmpty;
  }
  const RenderQueue &queue = core->game->rq;
  const uint32_t queued = queue.consumed ? 0u : (uint32_t)queue.n;
  return recipe.points.size() <= RQ_MAX - queued;
}

psxport::native_projection::ProjectionParams projection(Core *core, int clipRight) {
  psxport::native_projection::ProjectionParams out{};
  out.ofx = (int32_t)(core->rsub.projParams.geomOfx() * 65536.0f);
  out.ofy = (int32_t)(core->rsub.projParams.geomOfy() * 65536.0f);
  out.h = (uint16_t)core->rsub.projParams.geomH();
  if (gpu_vk_wide_engine(core)) {
    out.ofx = (clipRight / 2) << 16;
  }
  return out;
}

} // namespace

bool spyro_field_particles_submit(Core *core) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const auto recipe = spyro::field_particles_recipe::derive(ram);
  if (!preflight(core, recipe)) {
    lucent::debug("particles",
                  "REFUSED status={} records={} points={}",
                  spyro::field_particles_recipe::statusName(recipe.status),
                  recipe.records,
                  recipe.points.size());
    return false;
  }
  if (recipe.status == spyro::field_particles_recipe::Status::ValidEmpty) {
    return true;
  }

  const int clipRight = gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
  const auto camera = spyro::world_projection_math::decodeMatrix(ram, kCamera);
  const auto params = projection(core, clipRight);
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 0x2cu) >> 2;
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 0x30u) >> 2;
  RenderQueue &queue = core->game->rq;
  const GpuState &gpu = core->game->gpu;
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "particles:type0");
  for (const auto &point : recipe.points) {
    const auto input = spyro::world_projection_math::packProjectionInput(
        cameraY - point.y, cameraZ - point.z, point.x - cameraX);
    const auto projected = psxport::native_projection::project(camera, params, input);
    const int32_t otDepth = (int32_t)(projected.sz >> 5) - (int32_t)point.depthBias;
    const bool visible = projected.sz != 0u && projected.sz < 0x2000u && otDepth > 2 &&
                         projected.sx > 0 && projected.sx < clipRight && projected.sy > 0 &&
                         projected.sy < 256;
    core->mem_w8(point.address + 3u, visible ? 1u : 0u);
    if (!visible) {
      continue;
    }

    // RasterizeEmitList's type-0 arm emits LINE_G2 with SXY2 and SXY2+1. The shared queue carries
    // that primitive as two vertices, so the backend uses a real line-list draw rather than
    // changing the guest's one-pixel endpoint semantics into an invented quad.
    const int xs[2] = {projected.sx, projected.sx + 1};
    const int ys[2] = {projected.sy, projected.sy};
    const int us[2] = {}, vs[2] = {};
    const unsigned char rs[2] = {point.r, point.r};
    const unsigned char gs[2] = {point.g, point.g};
    const unsigned char bs[2] = {point.b, point.b};
    const float depth[2] = {core->rsub.projParams.pzToOrd(projected.pz),
                            core->rsub.projParams.pzToOrd(projected.pz)};
    core->game->gpu.s_seen3d = 1;
    queue.emitOrQueue(core,
                      1,
                      RQ_WORLD,
                      RQ_OM_DEPTH,
                      2,
                      0,
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
                      gpu.s_da_x1,
                      gpu.s_da_y1,
                      0);
  }
  lucent::debug("particles", "PASS records={} points={}", recipe.records, recipe.points.size());
  return true;
}
