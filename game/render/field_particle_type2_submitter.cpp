#include "field_particle_type2_submitter.h"

#include "core.h"
#include "field_particles_recipe.h"
#include "game.h"
#include "gpu_vk.h"
#include "particle_sine_table.h"
#include "producer_scope.h"
#include "proj_params.h"
#include "render_queue.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <cstddef>
#include <cstdint>
#include <lucent/log.h>
#include <span>

namespace {

constexpr uint32_t kProducerKey = 0x800573c8u;
constexpr uint32_t kCamera = 0x80076dd0u;

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

int16_t angleValue(uint16_t angle) {
  return spyro::particle_sine_table::values[(angle >> 1u) & 0xffu];
}

} // namespace

bool spyro_field_particle_type2_submit(Core *core,
                                       const spyro::field_particles_recipe::TexturedQuad &particle,
                                       unsigned ordinal) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const int clipRight = gpu_vk_wide_engine(core) ? gpu_vk_wide_engine_w(core) : 512;
  const auto camera = spyro::world_projection_math::decodeMatrix(ram, kCamera);
  const auto params = projection(core, clipRight);
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 0x2cu) >> 2;
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 0x30u) >> 2;
  const auto centerInput = spyro::world_projection_math::packProjectionInput(
      cameraY - particle.y, cameraZ - particle.z, particle.x - cameraX);
  const auto center = psxport::native_projection::project(camera, params, centerInput);
  const int16_t sine = angleValue(particle.angle);
  const int16_t cosine = angleValue((uint16_t)(particle.angle + 0x80u));
  const int halfWidth = ((int32_t)particle.size * sine) >> 10;
  const int halfHeight = ((int32_t)particle.size * cosine) >> 10;

  // The retained arm resets the rotation matrix to X=1, Y=0xA00/0x1000, Z=1 and retains the
  // pre-GPF first-pass MAC values as TR after shifting them left two. Re-run that fixed-point
  // projection for each source vertex instead of placing the quad with a screen-space shortcut.
  psxport::native_projection::FixedAffine billboard{};
  billboard.m = {{{0x1000, 0, 0}, {0, 0x0a00, 0}, {0, 0, 0x1000}}};
  for (size_t i = 0; i < billboard.t.size(); ++i) {
    billboard.t[i] = (int32_t)(center.raw_view_fixed[i] >> 12) * 4;
  }
  const int offsets[4][2] = {{-halfWidth, -halfHeight},
                             {halfHeight, -halfWidth},
                             {-halfHeight, halfWidth},
                             {halfWidth, halfHeight}};
  int xs[4]{}, ys[4]{};
  for (int i = 0; i < 4; ++i) {
    const auto vertex = psxport::native_projection::project(
        billboard, params, {(int16_t)offsets[i][0], (int16_t)offsets[i][1], 0});
    xs[i] = vertex.sx;
    ys[i] = vertex.sy;
  }
  const int us[4] = {(int)(particle.uvClut & 0xffu),
                     (int)(particle.uvTpage & 0xffu),
                     (int)(particle.uvClut & 0xffu),
                     (int)(particle.uvTpage & 0xffu)};
  const int vs[4] = {(int)((particle.uvClut >> 8) & 0xffu),
                     (int)((particle.uvClut >> 8) & 0xffu),
                     (int)((particle.uvTpage >> 8) & 0xffu),
                     (int)((particle.uvTpage >> 8) & 0xffu)};
  const unsigned char rs[4] = {(unsigned char)(particle.colorCommand & 0xffu),
                               (unsigned char)(particle.colorCommand & 0xffu),
                               (unsigned char)(particle.colorCommand & 0xffu),
                               (unsigned char)(particle.colorCommand & 0xffu)};
  const unsigned char gs[4] = {(unsigned char)((particle.colorCommand >> 8) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 8) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 8) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 8) & 0xffu)};
  const unsigned char bs[4] = {(unsigned char)((particle.colorCommand >> 16) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 16) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 16) & 0xffu),
                               (unsigned char)((particle.colorCommand >> 16) & 0xffu)};
  const float depth[4] = {core->rsub.projParams.pzToOrd(center.pz),
                          core->rsub.projParams.pzToOrd(center.pz),
                          core->rsub.projParams.pzToOrd(center.pz),
                          core->rsub.projParams.pzToOrd(center.pz)};
  const int clut = (int)((particle.uvClut >> 16) & 0xffffu);
  const int tpage = (int)((particle.uvTpage >> 16) & 0xffffu);
  const int mode = (tpage >> 7) & 3;
  const int32_t otDepth = (int32_t)(center.sz >> 5) - (int32_t)particle.depthBias;
  const bool visible = center.sz >= 0x80u && center.sz < 0x2000u && otDepth >= 0 && center.sx > 0 &&
                       center.sx < clipRight && center.sy > 0 && center.sy < 256;
  core->mem_w8(particle.address + 3u, visible ? 1u : 0u);
  if (!visible) {
    return true;
  }

  ProducerScope producer(&core->rsub.producerScope, kProducerKey, "particles:type2");
  core->game->gpu.s_seen3d = 1;
  core->game->rq.emitOrQueue(core,
                             1,
                             RQ_WORLD,
                             RQ_OM_DEPTH,
                             4,
                             ((particle.colorCommand >> 24) & 0x20u) != 0,
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
                             core->rsub.projParams.pzToOrd(center.pz),
                             0,
                             0,
                             {},
                             0,
                             (uint32_t)otDepth);
  lucent::debug("particles",
                "type2 ordinal={} address=0x{:08x} depth={}",
                ordinal,
                particle.address,
                otDepth);
  return true;
}
