#pragma once

#include <cstdint>

// The two GTE operations this title's renderers use to fade a colour toward a far colour, with
// sf=1 and lm=0. One transcription, because two copies of a GTE opcode diverge silently while both
// still produce a plausible colour.
namespace spyro::gte_color {

struct Vector3 {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
};

// INTPL over three already-unpacked IR components, returning the three accumulators.
Vector3 intpl(Vector3 ir, Vector3 farColor, int32_t ir0);

// DPCS: the same interpolation, sourced from a packed 0x00BBGGRR word and returned as one. Each
// accumulator drops its four fractional bits and clamps to a byte; the code byte rides through.
uint32_t dpcs(uint32_t rgb, Vector3 farColor, int32_t ir0);

} // namespace spyro::gte_color
