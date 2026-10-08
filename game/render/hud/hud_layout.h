// hud_layout.h — the guest's own HUD block, and the horizontal anchor class of each part of it.
//
// The guest authors the whole HUD in screen space against a 512-wide frame, and its placement
// tables are in the image (external/spyro-1 asm/data/math.data.s, `g_HudMobyTargetPos` at
// 0x8006E68C and `g_HudEggTargetRect` at 0x8006E71C; src/hud.c HudReset):
//
//   moby  0.. 3  gem digits       x =  90, 118, 146, 174    left side
//   moby     4   gem chest        x =  46                   left side
//   moby  5.. 6  dragon digits    x = 264, 292              centre
//   moby     7   dragon icon      x = 230                   centre
//   moby  8.. 9  life digits      x = 436, 464              right side
//   moby    10   Spyro head       x = 394                   right side (the life-orb sprites are
//                                 placed relative to it: HudReset `m_Mobys[10].x + orb.x - 29`)
//   moby    11   key              x = 430                   right side
//   rect  0..11  treasure row     x = 36 + 27*i             left side
//
// An element is classed by the side of the 512 frame its authored x sits on, because that is the
// inset the player sees: the gem chest is 46 px from the left edge and the life digits end 48 px
// from the right one. The class is a property of the layout and never of the aspect.
#pragma once

#include "ui_anchor.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace spyro::hud_layout {

inline constexpr std::uint32_t kHud = 0x80077fa8u;
inline constexpr std::uint32_t kMobys = kHud + 0x44u;
inline constexpr std::uint32_t kMobySize = 0x58u;
inline constexpr std::uint32_t kMobyCount = 12u;
inline constexpr std::uint32_t kSpriteRects = kHud + 0x464u;

// The treasure row (`m_SpriteRect[0..11]`) and the life orbs (`m_SpriteRect[12..]`, anchored to the
// Spyro head) are the two sprite families the collectables producer draws.
inline constexpr ui_anchor::Anchor kTreasureRowAnchor = ui_anchor::Anchor::LeftEdge;
inline constexpr ui_anchor::Anchor kLifeOrbAnchor = ui_anchor::Anchor::RightEdge;

struct MobyPart {
  ui_anchor::Anchor anchor;
  const char *element;
  std::size_t index;
};

// The part of the HUD a guest Moby record is, or nullopt for any record that is not one of the
// twelve `g_Hud.m_Mobys`.
constexpr std::optional<MobyPart> mobyPart(std::uint32_t actor) {
  if (actor < kMobys || actor >= kMobys + kMobyCount * kMobySize ||
      (actor - kMobys) % kMobySize != 0u) {
    return std::nullopt;
  }
  const std::size_t index = (actor - kMobys) / kMobySize;
  if (index <= 4u) {
    return MobyPart{ui_anchor::Anchor::LeftEdge, "hud-gem", index};
  }
  if (index <= 7u) {
    return MobyPart{ui_anchor::Anchor::Centred, "hud-dragon", index};
  }
  if (index <= 10u) {
    return MobyPart{ui_anchor::Anchor::RightEdge, "hud-lives", index};
  }
  return MobyPart{ui_anchor::Anchor::RightEdge, "hud-key", index};
}

} // namespace spyro::hud_layout
