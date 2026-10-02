// spyro3_hud_anchor.h — Spyro 3's in-level HUD widgets, the anchor class of each, and the native
// overrides that apply `spyro::ui_anchor` to the screen X they emit.
//
// THE DRAW CHAIN, measured in SCUS_944.67. The HUD pass is FUN_80029E48, called from FUN_8001E460
// under bit 0x20, and it dispatches one drawer per widget through the table at 0x8006727C that
// FUN_8002803C fills. FUN_800285A4 registers three groups:
//
//   FUN_8002803C(0x40, 3 widgets, init 80027E40, step 80027A60, DRAW 80029904, value *0x8006C71C)
//   FUN_8002803C(0x41, 4 widgets, init 80027E40, step 80027B0C, DRAW 80029BB0, value *0x8006C784)
//   FUN_8002803C(0x42, 5 widgets, init 80027E40, step 80027A60, DRAW 80029904, value *0x8006C660)
//
// Both counters come from the DRAW 0x80029904. Each drawer computes its own X from the element's
// first short and then emits, for every widget, an ICON through 0x800289C8 and a VALUE through
// 0x800291B8:
//
//   FUN_800289C8(icon, x)                    x = the element's X after FUN_80029674/FUN_80029708
//   FUN_800291B8(value, x, y, digitWidth)
//
// THE WIDGETS AND THEIR CLASSES, measured at run time and confirmed in the presented captures. The
// element address is the widget's own identity and the anchor class is a property of the widget, so
// this is the same shape of fact as Spyro 2's kGemCounter/kOrbCounter:
//
//   0x80067248  collectable (gem) counter  authored x 20   y 28   -> LeftEdge
//   0x8006729C  lives counter              authored x 256  y 28   -> Centred (authored ON 0x100)
//   0x800672F0  egg counter                authored x 492  y 28   -> RightEdge
//
// WHY THE CLASS CANNOT COME FROM THE RETURN ADDRESS HERE, and why this title needs the shared draw
// context where Spyro 2 does not: both counters reach the SAME two emitter call sites. Measured in
// one frame, the icon emitter's ra is 0x800299B8 for the gem counter at x 20 AND for the egg
// counter at x 382, and the value emitter's ra is 0x800299E4 for both (x 61 and x 423). One ra, two
// widgets, two opposite anchors.
//
// AND WHY THE ICON EMITTER IS NOT CORRECTED ON EVERY CALL: the value emitter does not draw its
// digits itself. FUN_800291B8 calls the ICON emitter FUN_800289C8 once per glyph from 0x80029374
// (return 0x80029378), so correcting the value emitter's X and then correcting the glyphs it hands
// down applies the margin TWICE. Measured at 684 with the correction applied at both: the
// collectable count lands at x -111 instead of -25 and is drawn off the left edge, so the number is
// not on screen at all; the egg's icon lands at 595 instead of 468 and is jammed against the right
// edge, which reads as the icon and the count having swapped sides. The value emitter therefore
// opens a scope of its own (`hud_draw_context::Draw::insideValue`) and the icon emitter leaves
// alone any call made inside it.
//
// WHERE IT IS APPLIED: at the emitters' X ARGUMENT, in a register, before any packet exists. No
// guest variable is read for the decision and none is written — the element address is the argument
// the drawer was already called with. At 4:3 every correction is zero, so both emitters are retail
// exactly.
#pragma once

#include "ui_anchor.h"

#include <cstdint>
#include <optional>

class Core;

namespace spyro3::hud_anchor {

// The two widget drawers FUN_800285A4 registers, and the two emitters both of them draw through.
inline constexpr std::uint32_t kCounterDrawer = 0x80029904u;
inline constexpr std::uint32_t kLivesDrawer = 0x80029BB0u;
inline constexpr std::uint32_t kIconEmitter = 0x800289C8u;
inline constexpr std::uint32_t kValueEmitter = 0x800291B8u;

// The widget elements FUN_8002803C registers, and what each one is anchored to.
inline constexpr std::uint32_t kGemCounter = 0x80067248u;
inline constexpr std::uint32_t kLivesCounter = 0x8006729Cu;
inline constexpr std::uint32_t kEggCounter = 0x800672F0u;

// The class of a HUD widget, or nullopt for an element this table does not name — which is most
// emitters in the game, since the icon and value emitters are reached from the 3D passes too.
constexpr std::optional<spyro::ui_anchor::Anchor> counterAnchor(std::uint32_t element) {
  switch (element) {
  case kGemCounter:
    return spyro::ui_anchor::Anchor::LeftEdge;
  case kLivesCounter:
    return spyro::ui_anchor::Anchor::Centred;
  case kEggCounter:
    return spyro::ui_anchor::Anchor::RightEdge;
  default:
    return std::nullopt;
  }
}

// Install the drawer and emitter overrides for this Core's resident SCUS_944.67 image.
void registerOverrides(Core &core);

} // namespace spyro3::hud_anchor