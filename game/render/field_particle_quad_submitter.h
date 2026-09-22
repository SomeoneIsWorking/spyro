#pragma once

#include "native_projection.h"

#include <cstdint>

struct Core;

namespace spyro::field_particles {

// EVERYTHING THE TYPE-2 AND TYPE-3 EMIT-LIST ARMS SHARE, which is everything except where the four
// corners land. Both read the same texture table, carry the same colour/command word at record
// +0x0C, map their UVs the same way, take the same screen and depth-range clip tests, write the
// same guest-visible byte at record +3, and emit the same POLY_FT4 into the same painter slot.
// Type 2 rotates one `size` through the sine table and re-projects four model-space offsets; type 3
// places an axis-aligned rectangle from two independent half-extents. That difference is the only
// thing the two arms own separately, so it is the only thing they implement separately.
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

// Clip, publish the guest-visible byte, and emit — given corners the caller has already placed.
// `xs`/`ys` are four screen-space positions in the guest's own order: top-left, top-right,
// bottom-left, bottom-right. Returns false only when the layer cannot be drawn at all; a particle
// that is merely off-screen is a true, drawn-nothing result.
bool submit(Core *core,
            const Quad &quad,
            const psxport::native_projection::NativeProjectedVertex &centre,
            const int xs[4],
            const int ys[4]);

} // namespace spyro::field_particles
