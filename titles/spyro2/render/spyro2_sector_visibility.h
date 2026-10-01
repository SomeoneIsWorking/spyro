// spyro2_sector_visibility.h — Spyro 2's terrain sector walk, SCUS_944.25 0x80043858..0x80044503,
// as a native override.
//
// WHAT RETAIL DOES. Once per drawn frame (one caller, FUN_8004C534 at 0x8004C55C, immediately
// before the sector drawer FUN_80044504) the walk visits every terrain sector record of the level
// and decides what the drawer will draw. It is hand-written assembly that saves every callee-saved
// register (and gp, sp, fp, ra) to 0x8006A9EC, borrows them all, and parks list cursors in HI/LO
// and in unused GTE light-matrix registers. For each record it:
//
//   1. skips it on its flag byte, or ends the walk on 0xFF;
//   2. culls it on a world-space box around the camera (draw distance from the record);
//   3. defers it to a side buffer when its mesh is not resident or its reach byte is negative (zero
//   skips it);
//   4. rotates its centre into view space and culls its bounding sphere on far, near, horizontal
//      and vertical planes, classifying it as edge-touching (1) or fully inside (2);
//   5. composes its orientation onto the camera rotation, tests an optional second sphere, and
//      builds a 0x44-byte render-list entry: mesh pointers, shading, depth bias, the composed
//      matrix, the projected centre and the classification.
//
// WHY IT IS OWNED. Step 4's horizontal test is the 4:3 half-angle (spyro2_sector_frustum.h). At
// 16:9 the projection centre is widened (spyro2_widescreen.h) and the sectors left of the authored
// window are culled here, so the left margin's ground ends in a staircase of sector edges. The
// override applies the widescreen plan's slope to every horizontal plane test; at 4:3 it is
// retail's.
//
// WHAT IT WRITES, all reproduced: the register save area, each visited record's classification
// byte (+0x4D), the render list (zero-terminated), the deferred-sector buffer (zero-terminated),
// the close list and its cursor (0x80068214), the far-shade watermark (0x80067168), and the GTE
// state it leaves for the drawer: VXY1/VZ1, the last rotation, TR, and the parked cursor registers.
// It returns with v0 = 0x8006820C and v1 holding its last scratch value, as retail does.
#pragma once

#include <cstdint>

class Core;

namespace spyro2 {

inline constexpr std::uint32_t kSectorVisibilityEntry = 0x80043858u;

// Install the override for this Core's resident SCUS_944.25 image.
void registerSectorVisibilityOverride(Core &core);

} // namespace spyro2
