// fairy_menu_recipe.h — what guest draw handler 0x8001D718 (GS_Fairy) paints over the world.
#pragma once

#include "pause_menu_recipe.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace spyro::fairy_menu {

inline constexpr std::uint32_t kPageCount = 8;

struct BoxRecord {
  std::int32_t x = 0;
  std::int32_t x2 = 0;
  std::int32_t y = 0;
  std::int32_t y2 = 0;
};

struct State {
  std::uint32_t state = 0;    // g_FairyCutscene.m_State, 1 = the dialogue is up
  std::uint32_t timer = 0;    // m_AnimationTimer, the wobble's phase source
  std::uint32_t selected = 0; // m_MenuSelectedOption
  std::uint32_t page = 0;     // m_MenuDialoguePage
  std::int32_t offsetX = 0;   // m_MenuOffsetX
  std::uint32_t cardSlot = 0; // m_MemoryCardSlot, 0 = slot 1
  std::array<BoxRecord, kPageCount> boxes{};
  std::uint32_t lightingPhase = 0;             // [0x800770F4]
  std::span<const std::uint8_t> directionRamp; // s_8006d82c ramp, as the pause menu reads it
};

// A caption exactly as the guest passes it to 0x800181AC.
struct Caption {
  std::uint32_t text = 0; // guest address of the NUL-terminated string
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
  pause_menu::Spacing spacing;
  std::int32_t spaceWidth = 0; // the builder's fifth argument (pSpaceWidth)
};

enum class Kind {
  WorldOnly, // m_State != 1: the handler draws the world and nothing else
  Dialogue,  // m_State == 1 and the page and selection are ones the handler's switch draws
  Refused    // m_State == 1 but the page or the selection is outside the guest's own switch
};

struct Recipe {
  Kind kind = Kind::WorldOnly;
  pause_menu::PanelRect panel;
  std::vector<pause_menu::Segment> border; // 4 lit edges, in the guest's call order
  std::vector<Caption> captions;           // in the guest's build order
  // The caption whose glyphs wobble. g_HudMobys is the arena's lowest address and the builder steps
  // it down per glyph, so the guest's wobble visits that caption's glyphs last character first.
  std::optional<std::size_t> wobbled;
};

inline constexpr std::uint8_t kCaptionShade = 11; // every call's fifth argument
inline constexpr std::uint8_t kPanelColour = 112; // setRGB0(f4, 112, 112, 112)
inline constexpr std::uint8_t kPanelStp = 1;      // code 0x2A: semi-transparent
inline constexpr std::int32_t kWobbleMultiplier = 3;
inline constexpr std::int32_t kWobbleShift = 9;
inline constexpr std::uint32_t kBoxTable = 0x8006F350u; // g_FairyDialogueBoxSizes

// A page of 8 or more has no box record and no case, and pages 0 and 6 read the selection to
// decide which caption wobbles. Both are `Refused`, never a guessed picture.
Recipe derive(const State &state);

} // namespace spyro::fairy_menu
