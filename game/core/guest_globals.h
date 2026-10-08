// guest_globals.h — the retail Spyro 1 globals named once for shared owners.
#pragma once

#include "guest_gp.h"

#include <cstdint>

namespace spyro::guest {

// g_Gamestate (common.h): the enum the main loop switches on.
inline constexpr std::uint32_t kGamestate = kGp + 0x574u; // 0x800757D8

// g_Camera: the occlusion group selects which collision groups the scene admits; the rotation is
// three halfwords read by both the camera and cutscene owners.
inline constexpr std::uint32_t kCamera = 0x80076DD0u;
inline constexpr std::uint32_t kCameraOcclusionGroup = kCamera + 0x54u; // 0x80076E24
inline constexpr std::uint32_t kCameraRotationX = kCamera + 0x4Cu;      // 0x80076E1C
inline constexpr std::uint32_t kCameraRotationY = kCamera + 0x4Eu;      // 0x80076E1E
inline constexpr std::uint32_t kCameraRotationZ = kCamera + 0x50u;      // 0x80076E20

// D_80077DD8: guest callees spill s0..s7, gp, sp, fp, ra here before reading arguments.
inline constexpr std::uint32_t kRegisterSpillArea = 0x80077DD8u;

// g_ModelSoundTables (moby.h): per-model-class pointer table indexed by moby +0x36.
inline constexpr std::uint32_t kModelSoundTables = 0x80076378u;

// g_Spyro (spyro.h): m_Position at +0, m_State at +0x78, m_controlFlags at +0x1F4.
inline constexpr std::uint32_t kSpyro = 0x80078A58u;
inline constexpr std::uint32_t kPlayerState = kSpyro + 0x78u;         // 0x80078AD0
inline constexpr std::uint32_t kPlayerControlFlags = kSpyro + 0x1F4u; // 0x80078C4C

// g_Environment (environment.h): +0x0C is the occlusion group count, +0x2C the collision header.
inline constexpr std::uint32_t kEnvironment = 0x800785A8u;
inline constexpr std::uint32_t kEnvironmentOcclusionGroupCount = kEnvironment + 0x0Cu;

// g_LoadStage (main.c): the streaming/load phase the main loop and the boot sequence both watch.
inline constexpr std::uint32_t kLoadStage = 0x80075864u;

// g_TitlescreenState (titlescreen.h): m_Mode through m_OptionSelected, in that order.
inline constexpr std::uint32_t kTitlescreenState = 0x80078D78u;

// g_LevelMobys (moby.h): pointer to the level's Moby array.
inline constexpr std::uint32_t kLevelMobys = 0x80075828u;

// D_8006CBF8 and D_8006CC78: sin and cos tables indexed with 12-bit angle.
inline constexpr std::uint32_t kSinTable = 0x8006CBF8u;
inline constexpr std::uint32_t kCosTable = 0x8006CC78u;

// D_800770C8: light colour matrix offsets for func_80020F34.
inline constexpr std::uint32_t kLightColorTable = 0x800770C8u;

// D_80074B84: the reciprocal-magnitude table the guest's normalisation reads after the GTE's
// leading-zero count.
inline constexpr std::uint32_t kMagnitudeTable = 0x80074B84u;

// g_LevelId and g_NextLevelId: current and requested level identifiers.
inline constexpr std::uint32_t kLevelId = 0x8007596Cu;
inline constexpr std::uint32_t kNextLevelId = 0x800758B4u;

// g_Portals (portal.h): up to six live portal pointers.
inline constexpr std::uint32_t kPortals = 0x80078640u;
inline constexpr std::uint32_t kPortalCount = 0x800758BCu;

// The main loop's clocks: g_LevelTicks per field from VSync, g_GameTick per GS_Playing update,
// g_UnprocessedFrames, g_DeltaTime, g_StateSwitch.
inline constexpr std::uint32_t kLevelTicks = 0x800758C8u;
inline constexpr std::uint32_t kGameTick = 0x8007572Cu;
inline constexpr std::uint32_t kUnprocessedFrames = 0x80075760u;
inline constexpr std::uint32_t kDeltaTime = 0x800756CCu;
inline constexpr std::uint32_t kStateSwitch = 0x8007579Cu;

// g_DemoMode (gamepad.h): DEMO_MODE_NONE/PLAY/RECORD.
inline constexpr std::uint32_t kDemoMode = 0x80075714u;

// g_Pad (gamepad.h): m_Down at +0, m_Released at +4, m_Held at +8.
inline constexpr std::uint32_t kPad = 0x80077378u;

// g_DragonCutscene (dragon.h): state machine after 0x24-byte WAD header.
inline constexpr std::uint32_t kDragonCutscene = 0x80077030u;

// Guest RAM is mirrored across KUSEG/KSEG0/KSEG1; indexing wants the offset into the image.
constexpr std::uint32_t ramOffset(std::uint32_t address) {
  return address & 0x1FFFFFu;
}

} // namespace spyro::guest
