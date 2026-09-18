// guest_globals.h — the retail Spyro 1 globals that more than one native owner reads, named once.
//
// WHY THIS EXISTS. Every module reaching into guest RAM used to spell these addresses itself:
// g_Camera was a local `constexpr` in eighteen files, g_Gamestate in seven under three different
// names, and the camera's occlusion group existed both as the literal 0x80076E24 and as
// `kCamera + 0x54`. Two spellings of one field is how a shipping owner and the oracle that checks
// it drift onto different memory — which is exactly what field_environment_recipe.h's own comment
// already warned about for its slice of them.
//
// Names are external/spyro-1's symbols over the byte-identical SCUS_942.28, so every constant here
// is checkable against that decomp instead of being a bare hex address nobody can re-derive. An
// address only one owner reads stays with that owner; this header is for the shared ones.
#pragma once

#include "guest_gp.h"

#include <cstdint>

namespace spyro::guest {

// g_Gamestate (common.h): the enum the main loop switches on, and gp-relative in the code that
// reads it (scene.cpp recovered the displacement from the compare chain).
inline constexpr std::uint32_t kGamestate = kGp + 0x574u; // 0x800757D8

// g_Camera (camera.h). The occlusion group selects which collision groups the scene admits; the
// handwritten query at 0x8004DF24 recomputes it every frame.
inline constexpr std::uint32_t kCamera = 0x80076DD0u;
inline constexpr std::uint32_t kCameraOcclusionGroup = kCamera + 0x54u; // 0x80076E24

// g_Spyro (spyro.h): m_Position at +0, m_State at +0x78.
inline constexpr std::uint32_t kSpyro = 0x80078A58u;

// g_Environment (environment.h): +0x0C is the occlusion group count, +0x2C the collision header.
inline constexpr std::uint32_t kEnvironment = 0x800785A8u;
inline constexpr std::uint32_t kEnvironmentOcclusionGroupCount = kEnvironment + 0x0Cu;

// Guest RAM is mirrored across KUSEG/KSEG0/KSEG1; a fixture that indexes a plain byte vector wants
// the offset into the 2 MiB image rather than the mapped address.
constexpr std::uint32_t ramOffset(std::uint32_t address) {
  return address & 0x1FFFFFu;
}

} // namespace spyro::guest
