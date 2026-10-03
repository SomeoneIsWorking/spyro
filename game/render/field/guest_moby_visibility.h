// guest_moby_visibility.h — the moby visibility walk of this engine family, as a native routine.
//
// ONE ROUTINE, TWO IMAGES. SCUS_944.25 0x80043858..0x80044503 and SCUS_944.67
// 0x80030478..0x80031123 are the same routine: both are 811 instructions, both decompile to the
// same C with the addresses and one `gp` spill different. The per-image words it reaches are a
// per-title fact table below, so the traversal and the arithmetic are one implementation.
//
// WHAT IT WALKS. The level's mobys -- the 0x58-byte object records at the list pointer named in
// `Facts.firstRecordWord`. The terrain is the other half of the frame's draw (see
// guest_terrain_drawer.h).
//
// WHAT RETAIL DOES. Once per drawn frame (one caller, the object half of the frame's draw
// FUN_8004C534 at 0x8004C55C for SCUS_944.25 / FUN_8001EC5C at 0x8001EC74 for SCUS_944.67,
// immediately before the moby drawers) the walk visits every moby of the level and decides what
// those drawers will draw. It is hand-written assembly that saves every callee-saved register (and
// gp, sp, fp, ra) to the register save area, borrows them all, and parks list cursors in HI/LO and
// in unused GTE light-matrix registers. For each record it:
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
// WHY IT IS OWNED. Step 4's horizontal test is the 4:3 half-angle (guest_moby_frustum.h). At 16:9
// the projection centre is widened (guest_widescreen_owner.h) and the mobys past the authored
// window are culled here, so the margins would show the scene without its objects. The routine
// applies the widescreen plan's slope to every horizontal plane test; at 4:3 it is retail's.
//
// WHAT IT WRITES, all reproduced: the register save area, each visited record's classification byte
// (+0x4D), the render list (zero-terminated), the deferred-moby buffer (zero-terminated), the close
// list and its cursor, the far-shade watermark, and the GTE state it leaves for the drawer: VXY1/
// VZ1, the last rotation, TR, and the parked cursor registers. It returns with v0 = the guest word
// `Facts.exitV0` and v1 holding its last scratch value, as retail does.
//
// An override is a plain `void (*)(Core *)`, so a title's facts cannot be captured by it. The title
// binds them in a one-line thunk of its own and calls `walk` below.
#pragma once

#include "core.h"
#include "guest_render_globals.h"

#include <cstdint>

namespace spyro::guest_moby {

struct Facts {
  // The walk's own entry, the address the native override is installed at.
  std::uint32_t entry = 0;
  // The name the framework's override differential and its logs identify this body by.
  const char *overrideName = "";

  std::uint32_t firstRecordWord = 0; // the level's moby array
  std::uint32_t drawerParameter = 0; // parked in VZ1 for the drawer that runs next
  std::uint32_t meshSlotTable = 0;   // per-mesh slot arrays
  std::uint32_t overlayTable = 0;    // overlay byte per record kind
  std::uint32_t closeCursorHome = 0; // close-list cursor, written at the exit
  std::uint32_t exitV0 = 0;          // v0 on return
  std::uint32_t sineTable = 0;       // the 256-entry sine table; the cosine table is +0x80 bytes
};

// Run one call of the walk against this image.
void walk(Core &core, const Facts &facts, const guest_render_globals::Globals &globals);

} // namespace spyro::guest_moby
