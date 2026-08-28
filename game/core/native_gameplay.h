// native_gameplay.h — small, source-backed gameplay ownership seams.
#pragma once

#include <cstdint>

namespace spyro::gameplay {

// Spyro's digital direction table is indexed by the four D-pad bits after they have been
// converted from the controller packet to the guest's active-high mask.
constexpr bool hasDigitalDirection(std::uint32_t held) {
  return (held & 0xF000u) != 0;
}

constexpr unsigned digitalDirectionTableIndex(std::uint32_t held) {
  return (held >> 12) & 0x0Fu;
}

} // namespace spyro::gameplay

void spyro_register_native_gameplay();
