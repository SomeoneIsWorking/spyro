#pragma once

#include "native_projection.h"

#include <array>
#include <cstdint>

struct Core;

namespace spyro::field_particles {

// EVERYTHING THE TEXTURED EMIT-LIST ARMS SHARE. All three read the same texture table, carry the
// same colour/command word at record +0x0C, map their UVs the same way onto the same four corners,
// write the same guest-visible byte at record +3, and emit the same POLY_FT4 into the same painter
// slot. What they own separately is where the corners land and what decides the quad is on screen:
// type 2 rotates one `size` through the sine table and re-projects four model-space offsets about a
// projected centre; type 3 places an axis-aligned rectangle from two independent half-extents about
// the same centre; the default arm rotates a square in the WORLD and projects all four corners, so
// it has no centre to test and sorts on the sum of four depths instead. `submit` below is the
// centre policy the first two share; `emit` is the packet all three share.
struct Quad {
  uint32_t address = 0;
  uint32_t scanOrdinal = 0;
  uint32_t colorCommand = 0;
  uint32_t uvClut = 0;
  uint32_t uvTpage = 0;
  uint8_t depthBias = 0;
  const char *what = ""; // producer-scope label, e.g. "particles:type2"
};

// The particle's centre, projected through the game's own camera and stated projection. Both arms
// need it before they can place anything, and the type-3 arm additionally scales its extents by
// this vertex's depth cue (`mac0`).
psxport::native_projection::NativeProjectedVertex
centre(Core *core, int16_t x, int16_t y, int16_t z);

// The four placed corners, in the guest's own order: top-left, top-right, bottom-left,
// bottom-right. `ord` is the host depth each corner draws at. The two centre-placed arms give all
// four the same value because their quad faces the camera; the world-oriented arm does not, because
// its does not.
struct Corners {
  std::array<int, 4> x{};
  std::array<int, 4> y{};
  std::array<float, 4> ord{};
};

// Emit one POLY_FT4 into the producer's painter scope. The caller has already decided that the quad
// is drawn and which ordering-table bucket it belongs in; everything below that decision — the
// texture-page and CLUT decode, the corner UV mapping, the flat colour and the queue call — is the
// same for every textured arm and lives here once.
void emit(Core *core, const Quad &quad, const Corners &corners, int32_t otDepth);

// Clip against the centre, publish the guest-visible byte, and emit — given corners the caller has
// already placed. Returns false only when the layer cannot be drawn at all; a particle that is
// merely off-screen is a true, drawn-nothing result.
bool submit(Core *core,
            const Quad &quad,
            const psxport::native_projection::NativeProjectedVertex &centre,
            const int xs[4],
            const int ys[4]);

} // namespace spyro::field_particles
