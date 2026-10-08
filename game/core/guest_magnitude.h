#pragma once

#include <cstdint>

namespace spyro::guest_magnitude {

// One normalise step of the reciprocal-magnitude lookup at 0x80074B84. Four shipping paths
// perform it (libgte's vector length 0x800171FC, scale-to-length 0x80017330, integer square root
// 0x80017A38, and the inline copy in r_moby's per-face lighting at 0x80021EB4); they differ only in
// which registers hold the value and where the leading-zero count comes from.
//
// `leadingZeros` is the guest's LZCR read: the library routines take it from the GTE, pure callers
// count host leading zeros, which agrees for the non-negative values they normalise.
struct Normalization {
  std::uint32_t evenLeadingZeros = 0; // LZCR rounded down to even
  std::uint32_t exponent = 0;         // (31 - evenLeadingZeros) >> 1, the halved magnitude
  std::uint32_t residualShift = 0;    // what the guest leaves in its shift register afterwards
  std::uint32_t tableByteOffset = 0;  // offset into 0x80074B84, already doubled for the halfword
};

// `value` must be non-zero; every guest caller branches around this on zero and leaves its result
// registers at 0.
Normalization normalize(std::uint32_t value, std::uint32_t leadingZeros);

// The guest's tail: the signed table halfword shifted back up by the halved magnitude, keeping the
// 12 fractional bits its callers then discard themselves.
std::uint32_t scaled(std::int16_t tableEntry, std::uint32_t exponent);

// What the GTE's LZCR would report for `value`: leading zeros when it is non-negative, leading ones
// when it is not. A caller holding a Core reads the real register instead; this is for the pure
// paths, and it must follow the same rule, because a sum of squares can carry into bit 31.
std::uint32_t lzcr(std::uint32_t value);

} // namespace spyro::guest_magnitude
