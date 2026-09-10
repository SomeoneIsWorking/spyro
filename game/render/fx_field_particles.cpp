#include "fx_field_particles.h"

#include "core.h"
#include "field_particle_type2_submitter.h"
#include "field_particles_recipe.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <algorithm>
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
  return recipe.points.size() + recipe.lines.size() + recipe.texturedQuads.size() <=
         RQ_MAX - queued;
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

// Both scanned emit-list arms end in a LINE_G2: the type-0 arm draws one pixel as SXY2..SXY2+1 and
// the type-1 arm draws between two projected endpoints. The queue carries that primitive as two
// vertices, so the backend uses a real line-list draw rather than inventing a quad.
void emitLine(Core *core,
              const int (&xs)[2],
              const int (&ys)[2],
              const unsigned char (&rs)[2],
              const unsigned char (&gs)[2],
              const unsigned char (&bs)[2],
              float ord,
              const PainterReplayOrder &order) {
  RenderQueue &queue = core->game->rq;
  const GpuState &gpu = core->game->gpu;
  const int us[2] = {}, vs[2] = {};
  const float depth[2] = {ord, ord};
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
                    0,
                    nullptr,
                    -1,
                    0.0f,
                    0,
                    0,
                    order);
}

} // namespace

bool spyro_field_particles_submit(Core *core) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const auto recipe = spyro::field_particles_recipe::derive(ram);
  if (!preflight(core, recipe)) {
    lucent::debug("particles",
                  "REFUSED status={} why={} type={} slot={:08X} records={} points={} lines={} "
                  "type2={}",
                  spyro::field_particles_recipe::statusName(recipe.status),
                  recipe.refusal,
                  recipe.refusedType,
                  recipe.refusedAddress,
                  recipe.records,
                  recipe.points.size(),
                  recipe.lines.size(),
                  recipe.texturedQuads.size());
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
  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "particles:emitlist");
  // Every other world producer publishes a painter object, and the queue refuses a flush that mixes
  // ordered and unordered items in one world run. Particles emitted unordered, which only stayed
  // invisible while no frame reached this producer with any of the others alive at the same time.
  RenderQueue::PainterObjectScope painter(core->game->rq, kProducerKey);
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
    const int xs[2] = {projected.sx, projected.sx + 1};
    const int ys[2] = {projected.sy, projected.sy};
    const unsigned char rs[2] = {point.r, point.r};
    const unsigned char gs[2] = {point.g, point.g};
    const unsigned char bs[2] = {point.b, point.b};
    emitLine(core,
             xs,
             ys,
             rs,
             gs,
             bs,
             core->rsub.projParams.pzToOrd(projected.pz),
             spyro::scene_painter_order::particle(
                 (uint16_t)std::clamp<int32_t>(otDepth, 0, 2047), point.scanOrdinal, 0u));
  }
  for (const auto &line : recipe.lines) {
    // The guest clips and depth-sorts the type-1 primitive on its first endpoint alone; the second
    // is projected only for its screen position, so a line whose far end leaves the frame still
    // draws exactly as retail draws it.
    const auto first = psxport::native_projection::project(
        camera,
        params,
        spyro::world_projection_math::packProjectionInput(
            cameraY - line.y0, cameraZ - line.z0, line.x0 - cameraX));
    const int32_t otDepth = (int32_t)(first.sz >> 5) - (int32_t)line.depthBias;
    const bool visible = first.sz != 0u && first.sz < 0x2000u && otDepth > 2 && first.sx > 0 &&
                         first.sx < clipRight && first.sy > 0 && first.sy < 256;
    core->mem_w8(line.address + 3u, visible ? 1u : 0u);
    if (!visible) {
      continue;
    }
    const auto second = psxport::native_projection::project(
        camera,
        params,
        spyro::world_projection_math::packProjectionInput(
            cameraY - line.y1, cameraZ - line.z1, line.x1 - cameraX));
    const int xs[2] = {first.sx, second.sx};
    const int ys[2] = {first.sy, second.sy};
    const unsigned char rs[2] = {line.r0, line.r1};
    const unsigned char gs[2] = {line.g0, line.g1};
    const unsigned char bs[2] = {line.b0, line.b1};
    emitLine(core,
             xs,
             ys,
             rs,
             gs,
             bs,
             core->rsub.projParams.pzToOrd(first.pz),
             spyro::scene_painter_order::particle(
                 (uint16_t)std::clamp<int32_t>(otDepth, 0, 2047), line.scanOrdinal, 0u));
  }
  for (const auto &quad : recipe.texturedQuads) {
    if (!spyro_field_particle_type2_submit(core, quad)) {
      return false;
    }
  }
  lucent::debug("particles",
                "PASS records={} points={} lines={} type2={}",
                recipe.records,
                recipe.points.size(),
                recipe.lines.size(),
                recipe.texturedQuads.size());
  return true;
}
