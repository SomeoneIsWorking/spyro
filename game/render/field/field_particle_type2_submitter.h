#pragma once

struct Core;

namespace spyro {
namespace field_particles_recipe {
struct TexturedQuad;
}
} // namespace spyro

// The caller owns the painter-object scope: all three emit-list arms belong to one producer and one
// scan, and the quad's place in that scan travels on the particle itself.
bool spyro_field_particle_type2_submit(Core *core,
                                       const spyro::field_particles_recipe::TexturedQuad &particle);
