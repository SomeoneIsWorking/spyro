// menu_lighting.h — the guest state 0x8001844C's border lighting reads.
//
// Every menu outline the guest draws lights each endpoint through 0x800169AC and 0x80017908: a
// direction ramp in the main image and the specular phase the field advances. The pause menu and
// the fairy dialogue read the same two things, so they are read in one place.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Core;

namespace spyro::menu_lighting {

// s_8006d82c, the ramp 0x800169AC indexes at `ratio + 0xDC`. 0xDC = 220 and the ratio's largest
// value is 64, so 65 bytes is the whole domain the guest can ask for.
inline constexpr std::uint32_t kDirectionRampBase = 0x8006D82Cu;
inline constexpr std::uint32_t kDirectionRampOffset = 0xDCu;
inline constexpr std::size_t kDirectionRampLength = 65;
// The border's palette rotation phase.
inline constexpr std::uint32_t kBorderLightingPhase = 0x800770F4u;

struct Lighting {
  std::array<std::uint8_t, kDirectionRampLength> ramp{};
  std::uint32_t phase = 0;
};

Lighting read(Core *core);

} // namespace spyro::menu_lighting
