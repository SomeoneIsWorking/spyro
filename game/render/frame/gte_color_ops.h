#pragma once

#include <cstdint>

// The two GTE operations this title's renderers use to fade a colour toward a far colour, derived
// from the hardware reference the port already vendors (beetle-psx mednafen/psx/gte.c) so the
// results are exact rather than close. sf=1, lm=0, which is what every caller encodes.
//
// They live here rather than inside one consumer because two unrelated subsystems run the same
// arithmetic: the world's interpolated animation channels blend authored keyframes with them, and
// the paired actor's colour-fade arm runs the whole material table through one of them before the
// ordinary parser reads it. One transcription, because two copies of a GTE opcode diverge silently
// — both would still produce a plausible colour.
namespace spyro::gte_color {

struct Vector3 {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

// INTPL over three already-unpacked IR components, returning the three accumulators.
Vector3 intpl(Vector3 ir, Vector3 farColor, int32_t ir0);

// DPCS: the same interpolation, but sourced from a packed 0x00BBGGRR word and returned as one. Each
// accumulator drops its four fractional bits and clamps to a byte on the way into the colour FIFO;
// the source word's code byte rides through untouched.
uint32_t dpcs(uint32_t rgb, Vector3 farColor, int32_t ir0);

} // namespace spyro::gte_color
