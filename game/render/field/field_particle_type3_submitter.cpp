#include "field_particle_type3_submitter.h"

#include "core.h"
#include "field_particle_quad_submitter.h"
#include "field_particles_recipe.h"

#include <cstdint>

bool spyro_field_particle_type3_submit(Core *core,
                                       const spyro::field_particles_recipe::SpriteQuad &particle) {
  const auto center = spyro::field_particles::centre(core, particle.x, particle.y, particle.z);
  // AXIS-ALIGNED IN SCREEN SPACE, unlike type 2. The retained arm (r_particles.s .L80057A74) takes
  // the RTPS depth cue straight out of MAC0 — `mfc2 MAC0 / srl 12 / mtc2 IR0`, a manual write that
  // skips the hardware's 0..1000h clamp on IR0 — loads the two size bytes into IR1/IR2, and runs
  // `GPF 0`, so each extent is (depth cue * size) >> 12 with no second projection and no rotation.
  const int32_t cue = (int32_t)((uint32_t)center.mac0 >> 12);
  const int width = (int)(((int64_t)cue * particle.sizeX) >> 12);
  const int height = (int)(((int64_t)cue * particle.sizeY) >> 12);
  // Then it places the far corner at the centre plus HALF each extent and walks back:
  //   xy3 = c + (w/2, h/2)   xy2 = xy3 - (w, 0)   xy1 = xy3 - (0, h)   xy0 = xy1 - (w, 0)
  // which is top-left, top-right, bottom-left, bottom-right — the order the shared UV mapping
  // assumes. The halves are the guest's own `srl 1` on each extent, not a rounding choice here.
  const int right = center.sx + (width >> 1);
  const int bottom = center.sy + (height >> 1);
  const int left = right - width;
  const int top = bottom - height;
  const int xs[4] = {left, right, left, right};
  const int ys[4] = {top, top, bottom, bottom};
  const spyro::field_particles::Quad quad{particle.address,
                                          particle.scanOrdinal,
                                          particle.colorCommand,
                                          particle.uvClut,
                                          particle.uvTpage,
                                          particle.depthBias,
                                          "particles:type3"};
  return spyro::field_particles::submit(core, quad, center, xs, ys);
}
