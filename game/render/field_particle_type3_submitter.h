#pragma once

struct Core;

namespace spyro {
namespace field_particles_recipe {
struct SpriteQuad;
}
} // namespace spyro

// The type-3 emit-list arm. Everything but the corner placement is shared with type 2; see
// field_particle_quad_submitter.h. The caller owns the painter-object scope.
bool spyro_field_particle_type3_submit(Core *core,
                                       const spyro::field_particles_recipe::SpriteQuad &particle);
