#include "field_particle_type2_submitter.h"

#include "core.h"
#include "field_particle_quad_submitter.h"
#include "field_particles_recipe.h"
#include "particle_sine_table.h"
#include "wide_screen_space.h"

#include <cstddef>
#include <cstdint>

namespace {

int16_t angleValue(uint16_t angle) {
  return spyro::particle_sine_table::values[(angle >> 1u) & 0xffu];
}

} // namespace

bool spyro_field_particle_type2_submit(
    Core *core, const spyro::field_particles_recipe::TexturedQuad &particle) {
  const auto center = spyro::field_particles::centre(core, particle.x, particle.y, particle.z);
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
  const auto params = spyro::wide_screen_space::projection(core);
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
  const spyro::field_particles::Quad quad{particle.address,
                                          particle.scanOrdinal,
                                          particle.colorCommand,
                                          particle.uvClut,
                                          particle.uvTpage,
                                          particle.depthBias,
                                          "particles:type2"};
  return spyro::field_particles::submit(core, quad, center, xs, ys);
}
