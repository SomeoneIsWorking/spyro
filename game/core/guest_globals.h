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

// g_LoadStage (main.c): the streaming/load phase the main loop and the boot sequence both watch.
inline constexpr std::uint32_t kLoadStage = 0x80075864u;

// g_TitlescreenState (titlescreen.h): m_Mode, m_State, m_Tick, m_SubTick, m_SubState,
// m_OptionSelected, in that order.
inline constexpr std::uint32_t kTitlescreenState = 0x80078D78u;

// g_LevelMobys (moby.h): pointer to the level's Moby array.
inline constexpr std::uint32_t kLevelMobys = 0x80075828u;

// D_800770C8. Offsets 0xC/0x10/0x14 hold the one light colour func_80020F34's per-face colour
// program splays across all three rows of the GTE light-colour matrix.
inline constexpr std::uint32_t kLightColorTable = 0x800770C8u;

// D_80074B84: the reciprocal-magnitude table the guest's normalisation reads after the GTE's
// leading-zero count. Named once here because three owners reach it under three different names.
inline constexpr std::uint32_t kMagnitudeTable = 0x80074B84u;

// g_LevelId is the level now resident; g_NextLevelId the one a portal entry has requested. The
// pair is how any owner tells "still here" from "a level change is under way".
inline constexpr std::uint32_t kLevelId = 0x8007596Cu;
inline constexpr std::uint32_t kNextLevelId = 0x800758B4u;

// g_Portals (portal.h) holds up to six Portal pointers; g_PortalCount says how many are live.
inline constexpr std::uint32_t kPortals = 0x80078640u;
inline constexpr std::uint32_t kPortalCount = 0x800758BCu;

// The main loop's own clocks. g_LevelTicks advances per field from the VSync callback;
// g_GameTick once per GS_Playing update; g_UnprocessedFrames counts fields the loop has not yet
// consumed; g_DeltaTime is the lag the last update was told about (main.c clamps it to 2..4);
// g_StateSwitch says this iteration's draw is skipped.
inline constexpr std::uint32_t kLevelTicks = 0x800758C8u;
inline constexpr std::uint32_t kGameTick = 0x8007572Cu;
inline constexpr std::uint32_t kUnprocessedFrames = 0x80075760u;
inline constexpr std::uint32_t kDeltaTime = 0x800756CCu;
inline constexpr std::uint32_t kStateSwitch = 0x8007579Cu;

// g_Pad (gamepad.h): m_Down at +0, m_Released at +4, m_Held at +8.
inline constexpr std::uint32_t kPad = 0x80077378u;

// g_DragonCutscene (dragon.h): a 0x24-byte WAD header, then the state machine.
inline constexpr std::uint32_t kDragonCutscene = 0x80077030u;

// Guest RAM is mirrored across KUSEG/KSEG0/KSEG1; a fixture that indexes a plain byte vector wants
// the offset into the 2 MiB image rather than the mapped address.
constexpr std::uint32_t ramOffset(std::uint32_t address) {
  return address & 0x1FFFFFu;
}

} // namespace spyro::guest
