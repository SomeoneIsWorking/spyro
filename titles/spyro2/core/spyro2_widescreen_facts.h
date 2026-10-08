// spyro2_widescreen_facts.h — SCUS_944.25's own projection facts, read out of its bytes.
//
// The widening itself is `spyro::GuestWidescreenOwner` (game/core/guest_widescreen_owner.h) and is
// shared with every other title of this engine family that presents its own guest VRAM. What is
// here is only what this IMAGE says, so a reader can re-derive each number instead of trusting it,
// and so the two titles cannot drift into two different widenings by editing a shared default.
//
// THE TWO MEASURED LIBRARY LEAVES. Both are `mtc2 rt, CR` -- the GTE's move-to-control-register
// form -- and both are reached by `jal`, which is what makes them legal override sites (an override
// resumes at `$ra`, captured on entry, so it is only correct where the guest arrived by a call):
//
//   80057AE8  48c4d000  mtc2 $a0, $26          ; SetGeomScreen(h): CR26 = h
//   80057AEC  03e00008  jr   $ra
//
//   80057AF8  00042400  sll  $a0, $a0, 16
//   80057AFC  00052c00  sll  $a1, $a1, 16
//   80057B00  48c4c000  mtc2 $a0, $24          ; SetGeomOffset(x, y): CR24 = x<<16, CR25 = y<<16
//   80057B04  48c5c800  mtc2 $a1, $25
//   80057B08  03e00008  jr   $ra
//
// The GTE register number is in bits 15-11, not in the bits 4-0 an ordinary COP2 form uses, which
// is why scanning an image the ordinary way finds nothing.
//
// THE AUTHORED WINDOW. The display bootstrap's own geometry-init leaf states it:
//
//   80011D24  addiu $sp,$sp,-0x18
//   80011D34  addiu $a0,$zero,0x100      ; 256
//   80011D38  jal   0x80057AF8           ; SetGeomOffset(256, ...)
//   80011D3C  addiu $a1,$zero,0x78       ; 120
//   80011D40  jal   0x80057AE8           ; SetGeomScreen(0x155)
//   80011D44  addiu $a0,$zero,0x155      ; 341
//
// so OFX = 256 for a 512-dot display mode (`gp1_display_width` decodes the NTSC 512 mode) and the
// guest's own view is 512 wide with its centre at half that.
//
// THE INLINE RESTATEMENTS. SCUS_944.25 also restates CR24/CR25 inline, at four sites that are NOT
// library leaves:
//
//   8004726C  sll $at,$at,16 ; 80047270  sll $v0,$v0,16
//   80047274  mtc2 $at,$24   ; 80047278  mtc2 $v0,$25
//   80047EA8  lui $at,0x100  ; 80047EAC  mtc2 $at,$24
//   80047EB0  lui $at,0x78   ; 80047EB4  mtc2 $at,$25
//
// (0x01000000 and 0x00780000 are 256.0 and 120.0 in the 16.16 the control registers hold -- the
// same pair the display bootstrap states through the two leaves.) They are deliberately left to the
// owner's per-field re-assertion rather than claimed as overrides, and the reason is the
// framework's own override contract: a native override resumes at `$ra`, captured when the override
// was entered, so it is only correct at an address the guest REACHED BY `jal`. An override planted
// on a mid-block `mtc2` would resume at the enclosing function's saved return address. These four
// sites are named here so the gap is stated rather than discovered as black margins.
#pragma once

#include "guest_widescreen_owner.h"

namespace spyro2 {

inline constexpr std::uint32_t kInlineProjectionPublications[] = {
    0x80047274u,
    0x80047278u,
    0x80047EACu,
    0x80047EB4u,
};

inline constexpr spyro::GuestWidescreenFacts kWidescreenFacts{
    .titleName = "Spyro 2",
    .setGeomOffsetOverrideName = "spyro2-set-geom-offset",
    .setGeomScreenOverrideName = "spyro2-set-geom-screen",
    .setGeomOffsetLeaf = 0x80057AF8u,
    .setGeomScreenLeaf = 0x80057AE8u,
    .setGeomOffsetSiteName = "0x80057AF8 SetGeomOffset",
    .setGeomScreenSiteName = "0x80057AE8 SetGeomScreen",
    .guestOffsetX = 256,
    .nativeWidth = 2 * 256,
    .nativeHeight = 240,
    .inlineProjectionPublications = kInlineProjectionPublications,
    .inlineProjectionPublicationCount =
        sizeof(kInlineProjectionPublications) / sizeof(kInlineProjectionPublications[0]),
};

} // namespace spyro2
