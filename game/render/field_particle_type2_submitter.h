#pragma once

struct Core;

namespace spyro {
namespace field_particles_recipe {
struct TexturedQuad;
}
} // namespace spyro

bool spyro_field_particle_type2_submit(Core *core,
                                       const spyro::field_particles_recipe::TexturedQuad &particle,
                                       unsigned ordinal);
