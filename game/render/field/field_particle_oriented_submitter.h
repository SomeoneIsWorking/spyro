#pragma once

#include <array>
#include <cstdint>

struct Core;

namespace spyro {
namespace field_particles_recipe {
struct OrientedQuad;
}
} // namespace spyro

// The emit-list arm at 0x800574F8 that every particle type from 6 up falls through to. It is the
// only one of the arms that orients its quad in the WORLD: one size byte scaled by a sine/cosine
// pair rotates a square about the particle's own position, and all four corners are projected
// through the camera. Types 2 and 3 project one centre and place their corners in screen space
// around it, so they can clip and depth-sort on that one vertex; this arm has no such vertex, and
// reproduces the guest's own bounding-box clip and four-depth sort key instead.
namespace spyro::field_particles_oriented {

// One corner as an offset from the particle's own position, on the first and third axes of the
// projection input. The second is never offset: the quad is flat in the world, not turned toward
// the camera.
struct Offsets {
  int32_t first = 0;
  int32_t third = 0;
};

// The four corners in the order the arm builds them: top-left, top-right, bottom-left,
// bottom-right. `across` is size * sin and `along` size * cos, and each corner takes one of them on
// each axis, which is what makes the square rotate about its centre rather than merely change size.
std::array<Offsets, 4> corners(int32_t across, int32_t along);

// Whether the quad's bounding box overlaps the screen, as the arm decides it: each side is tested
// independently, so it is enough that SOME corner is below the top edge, SOME corner above the
// bottom one, and so on. That is not the same as any one corner being inside, and a quad larger
// than the screen is exactly where the two answers differ.
bool boxOnScreen(const std::array<int16_t, 4> &xs,
                 const std::array<int16_t, 4> &ys,
                 int32_t clipRight);

} // namespace spyro::field_particles_oriented

// The caller owns the painter-object scope: all the emit-list arms belong to one producer and one
// scan, and the quad's place in that scan travels on the particle itself.
bool spyro_field_particle_oriented_submit(
    Core *core, const spyro::field_particles_recipe::OrientedQuad &particle);
