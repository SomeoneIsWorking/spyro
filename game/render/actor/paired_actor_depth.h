#pragma once

#include <cstdint>
#include <optional>

namespace spyro::paired_actor_depth {

struct Depth {
  double origin = 0;
  uint32_t near = 0;
  uint8_t shift = 0;
};

// 0x80023F8C..0x80023FD0: the depth bias is instance byte 39, control is model byte 11.
Depth derive(int32_t baseZ, uint8_t bias, uint32_t control);
// Extend the same arithmetic over matching source MAC-Z values. Only the original signed
// divide-by-128 boundary is quantized; the primitive depth origin remains continuous.
std::optional<Depth>
interpolate(int32_t previousZ, int32_t currentZ, uint8_t bias, uint32_t control, float t);

} // namespace spyro::paired_actor_depth
