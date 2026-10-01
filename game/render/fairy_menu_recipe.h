// fairy_menu_recipe.h — what guest draw handler 0x8001D718 (GS_Fairy) paints over the world, as a
// pure derivation over pre-GTE game state.
//
// THE HANDLER (draw.c:2041, decoded from the decompilation and checked against the image's string
// table). When g_FairyCutscene.m_State is 1 it builds a dialogue box with func_8001860C — a
// semi-transparent POLY_F4 (colour 112, GP0 0x2A, tpage abr 2) and the four 0x8001844C lit lines of
// its outline — then lays the dialogue page's captions with the proportional builder 0x800181AC
// (shade index 11), wobbles ONE caption's glyphs, and finally draws them through the shaded-moby
// pass. When m_State is not 1 the handler draws the world and nothing else.
//
// THE BOX RECTANGLE IS DATA, not code: g_FairyDialogueBoxSizes (0x8006F350) holds eight {x, x2, y,
// y2} short records indexed by the page, and every x is biased by m_MenuOffsetX (0 or 0xB0, chosen
// by the camera angle so the dialogue sits on the side the camera is not looking at). The records
// are read from the guest, so the box cannot drift from the binary.
//
// THE WOBBLE. Every page that wobbles does it the same way: `curMoby = g_HudMobys` is captured
// right after the wobbled caption is built, then `(curMoby++)->m_Rotation.z = COSINE_8(timer * 4 +
// i * 12) * 3 >> 9` for textLen glyphs. g_HudMobys is the arena's LOWEST address and the builder
// steps it DOWN once per glyph, so the loop visits that caption's glyphs LAST CHARACTER FIRST. The
// recipe names which caption wobbles and the scene applies the guest's order.
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
  // The caption whose glyphs wobble, as an index into `captions`; none when the page wobbles
  // nothing (it never happens for a valid page: every page wobbles exactly one caption).
  std::optional<std::size_t> wobbled;
};

inline constexpr std::uint8_t kCaptionShade = 11; // every call's fifth argument
inline constexpr std::uint8_t kPanelColour = 112; // setRGB0(f4, 112, 112, 112)
inline constexpr std::uint8_t kPanelStp = 1;      // code 0x2A: semi-transparent
inline constexpr std::int32_t kWobbleMultiplier = 3;
inline constexpr std::int32_t kWobbleShift = 9;
inline constexpr std::uint32_t kBoxTable = 0x8006F350u; // g_FairyDialogueBoxSizes

// The page the guest's switch handles: a page of 8 or more has no box record and no case, and
// pages 0 and 6 read the selection to decide which caption wobbles, so a selection outside their
// options leaves the guest's `textLen` uninitialised. Both are `Refused`, never a guessed picture.
Recipe derive(const State &state);

} // namespace spyro::fairy_menu
