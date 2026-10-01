// spyro2_moby_rotation.h — a moby's orientation, composed onto the camera rotation by Spyro 2's
// moby visibility walk (SCUS_944.25 0x80043858, 80043B9C..80043D7C and 800441BC..800444B0; see
// spyro2_moby_visibility.h).
//
// A moby carries three 8-bit angles (bytes 2, 1, 0 of its rotation word, 256 steps per
// turn). For each non-zero angle the walk rotates the current GTE rotation matrix about one axis:
// it feeds (cos, sin) through two `RT * V0` products and repacks the six results into the five
// halfword-paired RT words, which it then loads back into CR0..CR4 so the next angle composes onto
// this one. The sine table is at 0x80061BD8 and the cosine table is the same table a quarter turn
// on (+0x80 bytes).
//
// TWO ANGLE SOURCES, one composition. A plain moby reads the tables directly with `lhu`, so the
// values are zero-extended. A moby whose flags set bit 30 adds a 4-bit fraction per angle from
// its flags word and interpolates between neighbouring entries with `lh`, so its values are
// sign-extended. The two are not interchangeable: a negative cosine packed as the low half of a
// GTE word sets the high half to 0xFFFF only in the interpolated case, which changes the product.
// Each source therefore hands the composition the exact 32-bit register words the image holds.
#pragma once

#include <array>
#include <cstdint>

class Core;

namespace spyro2::moby_rotation {

// The five GTE rotation words RT11RT12, RT13RT21, RT22RT23, RT31RT32, RT33.
using RotationWords = std::array<std::uint32_t, 5>;

// Retail's guest register words for one angle.
struct AngleWords {
  std::uint32_t sine = 0;
  std::uint32_t cosine = 0;
};

inline constexpr std::uint32_t kSineTable = 0x80061BD8u;
inline constexpr std::uint32_t kCosineOffset = 0x80u;

// Rotate `rotation` by the angles in `angleWord`, reading the tables directly. Loads the result
// into CR0..CR4 after each angle, as retail does. `rotation` must hold what CR0..CR4 hold on entry.
void composeTabled(Core &core, std::uint32_t angleWord, RotationWords &rotation);

// The same, with each angle refined by the matching 4-bit fraction of `flags` (bits 16, 8, 0).
void composeInterpolated(Core &core,
                         std::uint32_t angleWord,
                         std::uint32_t flags,
                         RotationWords &rotation);

// Bit 28 of the flags mirrors the composed matrix's second column before it is stored. Retail
// negates RT12, RT22 and RT32 in the stored copy only; CR0..CR4 keep the unmirrored matrix.
void mirror(RotationWords &rotation);

} // namespace spyro2::moby_rotation
