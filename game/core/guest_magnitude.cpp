#include "guest_magnitude.h"

namespace spyro::guest_magnitude {

Normalization normalize(std::uint32_t value, std::uint32_t leadingZeros) {
  Normalization out{};
  out.evenLeadingZeros = leadingZeros & ~1u;
  out.exponent = (std::uint32_t)((std::int32_t)(31 - (std::int32_t)out.evenLeadingZeros) >> 1);
  const std::int32_t excess = (std::int32_t)out.evenLeadingZeros - 24;
  std::uint32_t normalized = 0;
  if (excess < 0) {
    // The guest's `neg`/`srav` pair: an ARITHMETIC right shift, and the register keeps the
    // distance.
    out.residualShift = 24u - out.evenLeadingZeros;
    normalized = (std::uint32_t)((std::int32_t)value >> (int)(out.residualShift & 31u));
  } else {
    normalized = value << ((std::uint32_t)excess & 31u);
    // The constant 24 lands in the register from a branch delay slot, so it is what the guest is
    // left holding here rather than the distance it actually shifted by.
    out.residualShift = 24u;
  }
  out.tableByteOffset = (normalized - 64u) << 1;
  return out;
}

std::uint32_t scaled(std::int16_t tableEntry, std::uint32_t exponent) {
  return (std::uint32_t)((std::int32_t)tableEntry << (exponent & 31u));
}

std::uint32_t lzcr(std::uint32_t value) {
  const std::uint32_t counted = (value & 0x80000000u) != 0u ? ~value : value;
  return counted == 0u ? 32u : (std::uint32_t)__builtin_clz(counted);
}

} // namespace spyro::guest_magnitude
