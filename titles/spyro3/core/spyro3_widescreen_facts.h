// spyro3_widescreen_facts.h — SCUS_944.67's own projection facts, read out of its bytes.
//
// The widening itself is `spyro::GuestWidescreenOwner` (game/core/guest_widescreen_owner.h), the
// same owner Spyro 2 uses; only this IMAGE's measured sites are here.
//
// THE MEASURED LIBRARY LEAF. SetGeomOffset is this image's `PlatformHlePlan.setGeomOffset`, and
// the bytes are the plan's own:
//
//   8002A9AC  addiu $a0,$zero,0x100 ; 8002A9B4 addiu $a1,$zero,0x78
//   8002A9B0  jal  0x8005D35C
//   8005D35C  sll  $a0, $a0, 0x10
//   8005D360  sll  $a1, $a1, 0x10
//   8005D364  48c4c000               mtc2 $a0, CR24
//   8005D368  48c5c800               mtc2 $a1, CR25
//   8005D36C  jr   $ra
//
// which is the only `mtc2 rt, CR24` in the whole resident image reachable with the shift pair, and
// the geometry init calls it and nothing else in the image does.
//
// WHY THERE IS NO SETGEOMSCREEN SITE HERE, which is a measurement and not an omission. This image
// has exactly ONE `mtc2 rt, CR26` in its resident text, at 0x8005955C -- the same single-site shape
// Spyro 2 has at its own 0x80057AE8 -- and the argument it is given is computed at the call site:
//
//   80012064  lui  $a0, 0x8007
//   80012068  lw   $a0, -0x1e30($a0)      ; 0x8006E1D0, the camera's own distance bias
//   8001206C  jal  0x8005955C
//   80012070  addiu $a0, $a0, 0x155       ; + 341, the same 0x155 the geometry init passes
//
// CR26 is the projection DISTANCE, and the plan never writes it: widening H divides the guest's own
// perspective maths and moves guest-visible geometry for no presentation gain. So this title needs
// no second site, and none is claimed. Naming the site in the facts as zero is what makes that
// decision explicit rather than a silent omission.
//
// THE AUTHORED WINDOW. The geometry init states the same pair Spyro 2's does -- (0x100, 0x78) into
// SetGeomOffset and 0x155 into the distance leaf -- so OFX = 256 for the 512-dot display mode and
// the guest's own view is 512 wide with its centre at half that.
//
// THE INLINE RESTATEMENTS. Four sites restate CR24/CR25 inline, each with the same shape as Spyro
// 2's: two in the moby drawer that restores the projection after a far draw (0x80033EF8/0x80033EFC,
// shifted from a computed pair) and two that state 256/120 outright (0x80034B30/0x80034B38). None
// is reached by `jal`, so none is claimed as an override; the owner's per-field re-assertion is
// what puts the widened centre back, and naming them here says so rather than leaving black margins
// to be discovered.
#pragma once

#include "guest_widescreen_owner.h"

namespace spyro3 {

inline constexpr std::uint32_t kInlineProjectionPublications[] = {
    0x80033EF8u,
    0x80033EFCu,
    0x80034B30u,
    0x80034B38u,
};

inline constexpr spyro::GuestWidescreenFacts kWidescreenFacts{
    .titleName = "Spyro 3",
    .setGeomOffsetOverrideName = "spyro3-set-geom-offset",
    .setGeomScreenOverrideName = "",
    .setGeomOffsetLeaf = 0x8005D35Cu,
    // Zero: see the header. This image's single CR26 site is a projection-distance publication the
    // widening never touches, so claiming it would bind a leaf whose only effect is one the plan
    // must not have.
    .setGeomScreenLeaf = 0,
    .setGeomOffsetSiteName = "0x8005D35C SetGeomOffset",
    .setGeomScreenSiteName = "",
    .guestOffsetX = 256,
    .nativeWidth = 2 * 256,
    .nativeHeight = 240,
    .inlineProjectionPublications = kInlineProjectionPublications,
    .inlineProjectionPublicationCount =
        sizeof(kInlineProjectionPublications) / sizeof(kInlineProjectionPublications[0]),
};

} // namespace spyro3
