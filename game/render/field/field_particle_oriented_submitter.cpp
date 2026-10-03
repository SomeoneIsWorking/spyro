#include "field_particle_oriented_submitter.h"

#include "core.h"
#include "field_particle_quad_submitter.h"
#include "field_particles_recipe.h"
#include "guest_globals.h"
#include "particle_sine_table.h"
#include "proj_params.h"
#include "wide_screen_space.h"
#include "world_chunk_codec.h"
#include "world_projection_math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using spyro::guest::kCamera;

// The guest compares whole PACKED SXY words, so two of its four screen-edge tests are comparisons
// on (sy << 16) | (uint16)sx rather than on a coordinate. They are reproduced as the packed word
// because the top edge's threshold makes that row depend on the column: a corner at row 1 and
// column 0 fails it, and the same corner one pixel to the right passes.
int32_t packedScreen(int16_t x, int16_t y) {
  return (int32_t)(((uint32_t)(uint16_t)y << 16) | (uint32_t)(uint16_t)x);
}

} // namespace

namespace spyro::field_particles_oriented {

std::array<Offsets, 4> corners(int32_t across, int32_t along) {
  return {Offsets{-across, along},
          Offsets{along, across},
          Offsets{-along, -across},
          Offsets{across, -along}};
}

bool boxOnScreen(const std::array<int16_t, 4> &xs,
                 const std::array<int16_t, 4> &ys,
                 int32_t clipRight) {
  bool belowTop = false;
  bool aboveBottom = false;
  bool rightOfLeft = false;
  bool leftOfRight = false;
  for (std::size_t i = 0; i < xs.size(); ++i) {
    const int32_t packed = packedScreen(xs[i], ys[i]);
    belowTop = belowTop || packed > 0x10000;
    aboveBottom = aboveBottom || packed < 0x1000000;
    rightOfLeft = rightOfLeft || xs[i] > 0;
    leftOfRight = leftOfRight || xs[i] < clipRight;
  }
  return belowTop && aboveBottom && rightOfLeft && leftOfRight;
}

} // namespace spyro::field_particles_oriented

bool spyro_field_particle_oriented_submit(
    Core *core, const spyro::field_particles_recipe::OrientedQuad &particle) {
  const spyro::world_chunk_codec::RamView ram(std::span<const uint8_t>(core->ram));
  const auto camera = spyro::world_projection_math::decodeMatrix(ram, kCamera);
  const auto params = spyro::wide_screen_space::projection(core);
  const int32_t cameraX = (int32_t)core->mem_r32(kCamera + 0x28u) >> 2;
  const int32_t cameraY = (int32_t)core->mem_r32(kCamera + 0x2cu) >> 2;
  const int32_t cameraZ = (int32_t)core->mem_r32(kCamera + 0x30u) >> 2;

  // GPF with IR0 = size and IR1/IR2 = the sine pair, then an arithmetic shift of 12. The cosine
  // entry is the sine entry 64 further along the same table, which is why the table carries 320
  // entries for a 256-entry period.
  const int32_t sine = spyro::particle_sine_table::values[particle.angle];
  const int32_t cosine = spyro::particle_sine_table::values[particle.angle + 64u];
  const int32_t across = ((int32_t)particle.size * sine) >> 12;
  const int32_t along = ((int32_t)particle.size * cosine) >> 12;

  std::array<int16_t, 4> xs{};
  std::array<int16_t, 4> ys{};
  spyro::field_particles::Corners placed{};
  int32_t depthSum = 0;
  const auto offsets = spyro::field_particles_oriented::corners(across, along);
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    const auto projected = psxport::native_projection::project(
        camera,
        params,
        spyro::world_projection_math::packProjectionInput(cameraY - particle.y + offsets[i].first,
                                                          cameraZ - particle.z,
                                                          particle.x - cameraX + offsets[i].third));
    xs[i] = projected.sx;
    ys[i] = projected.sy;
    placed.x[i] = projected.sx;
    placed.y[i] = projected.sy;
    // Each corner stands at its own distance, because the quad is oriented in the world rather than
    // toward the camera. The guest packet cannot say so — it carries one ordering-table bucket for
    // the whole primitive — but the host rasterizer can, and these are the projected depths the
    // same four vertices already produced.
    placed.ord[i] = core->rsub.projParams.pzToOrd(projected.pz);
    depthSum += (int32_t)projected.sz;
  }

  // The sort key is the sum of all four depths, not one representative vertex, and its range test
  // is on that sum: below 0x200 or at 0x8000 and above, the arm draws nothing at all.
  const int32_t otDepth = (depthSum >> 7) - (int32_t)particle.depthBias;
  const bool depthOk = depthSum >= 0x200 && depthSum < 0x8000 && otDepth >= 0;

  // See wide_screen_space.h: the guest's byte is decided by the guest's own horizontal window, the
  // draw by the widened one. Writing the widened answer into guest memory is how widescreen once
  // changed what the game believed was on screen.
  std::array<int16_t, 4> guestXs{};
  for (std::size_t i = 0; i < guestXs.size(); ++i) {
    guestXs[i] = (int16_t)spyro::wide_screen_space::guestX(core, xs[i]);
  }
  const bool guestVisible = depthOk && spyro::field_particles_oriented::boxOnScreen(
                                           guestXs, ys, spyro::wide_screen_space::kGuestClipRight);
  core->mem_w8(particle.address + 3u, guestVisible ? 1u : 0u);

  if (!depthOk || !spyro::field_particles_oriented::boxOnScreen(
                      xs, ys, spyro::wide_screen_space::drawClipRight(core))) {
    return true;
  }
  const spyro::field_particles::Quad quad{particle.address,
                                          particle.scanOrdinal,
                                          particle.colorCommand,
                                          particle.uvClut,
                                          particle.uvTpage,
                                          particle.depthBias,
                                          "particles:oriented"};
  spyro::field_particles::emit(core, quad, placed, otDepth);
  return true;
}
