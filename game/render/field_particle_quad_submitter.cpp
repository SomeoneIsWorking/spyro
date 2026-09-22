#include "field_particle_quad_submitter.h"
#include "guest_globals.h"

#include "core.h"
#include "game.h"
#include "gpu_vk.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "render_queue.h"
#include "scene_painter_order.h"
#include "wide_screen_space.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <lucent/log.h>
#include <span>

namespace spyro::field_particles {
namespace {

constexpr uint32_t kProducerKey = 0x800573c8u;
using spyro::guest::kCamera;

} // namespace

psxport::native_projection::NativeProjectedVertex
centre(Core *core, int16_t x, int16_t y, int16_t z) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const auto camera = spyro::world_projection_math::decodeMatrix(ram, kCamera);
  const auto params = spyro::wide_screen_space::projection(core);
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 0x2cu) >> 2;
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 0x30u) >> 2;
  return psxport::native_projection::project(
      camera,
      params,
      spyro::world_projection_math::packProjectionInput(cameraY - y, cameraZ - z, x - cameraX));
}

void emit(Core *core, const Quad &quad, const Corners &corners, int32_t otDepth) {
  const int us[4] = {(int)(quad.uvClut & 0xffu),
                     (int)(quad.uvTpage & 0xffu),
                     (int)(quad.uvClut & 0xffu),
                     (int)(quad.uvTpage & 0xffu)};
  const int vs[4] = {(int)((quad.uvClut >> 8) & 0xffu),
                     (int)((quad.uvClut >> 8) & 0xffu),
                     (int)((quad.uvTpage >> 8) & 0xffu),
                     (int)((quad.uvTpage >> 8) & 0xffu)};
  const auto channel = [&](unsigned shift) {
    return (unsigned char)((quad.colorCommand >> shift) & 0xffu);
  };
  const unsigned char rs[4] = {channel(0), channel(0), channel(0), channel(0)};
  const unsigned char gs[4] = {channel(8), channel(8), channel(8), channel(8)};
  const unsigned char bs[4] = {channel(16), channel(16), channel(16), channel(16)};
  const int clut = (int)((quad.uvClut >> 16) & 0xffffu);
  const int tpage = (int)((quad.uvTpage >> 16) & 0xffffu);
  const int mode = (tpage >> 7) & 3;

  ProducerScope producer(&core->rsub.producerScope, kProducerKey, quad.what);
  core->game->gpu.s_seen3d = 1;
  core->game->rq.emitOrQueue(
      core,
      1,
      RQ_WORLD,
      RQ_OM_DEPTH,
      4,
      ((quad.colorCommand >> 24) & 0x20u) != 0,
      0,
      corners.x.data(),
      corners.y.data(),
      nullptr,
      nullptr,
      us,
      vs,
      rs,
      gs,
      bs,
      corners.ord.data(),
      mode,
      (tpage & 0xf) * 64,
      ((tpage >> 4) & 1) * 256,
      (clut & 0x3f) * 16,
      (clut >> 6) & 0x1ff,
      core->game->gpu.s_tw_mx,
      core->game->gpu.s_tw_my,
      core->game->gpu.s_tw_ox,
      core->game->gpu.s_tw_oy,
      core->game->gpu.s_da_x0,
      core->game->gpu.s_da_y0,
      core->game->gpu.s_da_x1,
      core->game->gpu.s_da_y1,
      (tpage >> 5) & 3,
      nullptr,
      -1,
      corners.ord[0],
      0,
      0,
      spyro::scene_painter_order::particle(
          (uint16_t)std::clamp<int32_t>(otDepth, 0, 2047), quad.scanOrdinal, 0u),
      0,
      (uint32_t)otDepth);
  lucent::debug("particles",
                "{} ordinal={} address=0x{:08x} depth={}",
                quad.what,
                quad.scanOrdinal,
                quad.address,
                otDepth);
}

bool submit(Core *core,
            const Quad &quad,
            const psxport::native_projection::NativeProjectedVertex &center,
            const int xs[4],
            const int ys[4]) {
  const int clipRight = spyro::wide_screen_space::drawClipRight(core);
  const float ord = core->rsub.projParams.pzToOrd(center.pz);
  const int32_t otDepth = (int32_t)(center.sz >> 5) - (int32_t)quad.depthBias;
  // See wide_screen_space.h: the guest's byte uses the guest's horizontal window, the draw uses
  // the widened one. They were the same value, so widescreen wrote different guest memory.
  const bool depthAndRowOk =
      center.sz >= 0x80u && center.sz < 0x2000u && otDepth >= 0 && center.sy > 0 && center.sy < 256;
  const bool guestVisible =
      depthAndRowOk && spyro::wide_screen_space::guestOnScreenX(core, center.sx);
  core->mem_w8(quad.address + 3u, guestVisible ? 1u : 0u);
  const bool visible =
      depthAndRowOk && spyro::wide_screen_space::drawnOnScreenX(clipRight, center.sx);
  if (!visible) {
    return true;
  }
  Corners corners{};
  for (size_t i = 0; i < corners.x.size(); ++i) {
    corners.x[i] = xs[i];
    corners.y[i] = ys[i];
    corners.ord[i] = ord;
  }
  emit(core, quad, corners, otDepth);
  return true;
}

} // namespace spyro::field_particles
