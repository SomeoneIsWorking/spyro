// spyro3_render_facts.h — SCUS_944.67's own render-routine facts, read out of its bytes, plus the
// two native overrides that bind them.
//
// The routines themselves live in game/render/ and are shared with SCUS_944.25. That is a measured
// claim, not an assumed one: the terrain drawer (0x80022378 here, 0x80023BB4 there) is 5,487
// instructions in BOTH images and the two decompilations differ in exactly one line; the moby
// visibility walk (0x80030478 here, 0x80043858 there) is 811 instructions in both and the two
// decompilations differ only in their addresses and one `gp` spill. Every address below was then
// located in THIS image's own bytes, either at the instruction the comment names or at the
// offset-aligned counterpart of the Spyro 2 site (the two routines are instruction-identical, so
// instruction N of one is instruction N of the other, and each address below was read out of that
// instruction in this image rather than derived by arithmetic).
#pragma once

#include "core.h"
#include "guest_moby_visibility.h"
#include "guest_render_globals.h"
#include "guest_terrain_drawer.h"
#include "guest_terrain_facts.h"
#include "native_execution.h"

namespace spyro3 {

// The guest globals both routines share.
//
//   80071540  the register save area. The walk's prologue at 80030478..800304AC is word-for-word
//             the Spyro 2 prologue at 80043858..8004388C -- `sw $s0..$s7, $gp, $sp, $fp, $ra` at
//             0x20..0x2C -- with the base `lui $at,0x8007 ; addiu $at,$at,0x1540`. The terrain
//             drawer's prologue spills the same twelve words to the same base.
//   8006C668  the frame's scratch block end: the walk reads it at 800304B8 and derives every list
//             base from it; the drawer derives its sector-list base from it at 800223B8.
//   8006C664  next free primitive packet, written by the drawer's exit.
//   8006C634  the ordering table, whose 8-byte depth bins both routines link into.
//   8006C7D4  the far-shade watermark, lowered only by the walk.
//   8006DFF8  the camera rotation, five RT words: the walk's own `lui $t9,0x8007 ; addiu
//             $t9,$t9,-0x2008` at 80030514..80030518.
//   8006E020  the camera position x, y, z.
//   80072330  one visibility byte per terrain sector, written by the classification pass and read
//             by the walk for each moby's group byte.
inline constexpr spyro::guest_render_globals::Globals kRenderGlobals{
    .registerSaveArea = 0x80071540u,
    .scratchBaseWord = 0x8006C668u,
    .primitiveCursor = 0x8006C664u,
    .orderingTable = 0x8006C634u,
    .orderingTableMark = 0x8006C7D4u,
    .cameraRotation = 0x8006DFF8u,
    .cameraPosition = 0x8006E020u,
    .visibilityGroups = 0x80072330u,
};

// The terrain drawer, 0x80022378.
//
//   800223C8  jal 0x8002D0D8      the drawer's only call out of its own body
//   800223D8  sw  $v0, ...        its v0 lands in the visible-sector count word 0x8006D078
//   800223B8  the scratch-list base: [0x8006C668] - 0x3000
//
// Its caller is FUN_8001EC24 (0x8001EC24), which is the structural twin of Spyro 2's FUN_8004C4FC:
// each clears the same 0x1C00 scratch block and then calls its drawer, and neither has any other
// call.
inline constexpr spyro::guest_terrain::Facts kTerrainFacts{
    .entry = 0x80022378u,
    .overrideName = "spyro3-terrain-drawer",
    .visibilityCallSite = 0x800223C8u,
    .sectorVisibility = 0x8002D0D8u,
    .visibleSectorCount = 0x8006D078u,
    .scratchListsBelowEnd = 0x3000u,
    .fogColour = 0x8006D07Cu,     // RFC, GFC, BFC words
    .foggedColours = 0x8006E588u, // the faded copy of a sector's colours
    // The geometry init states SetGeomOffset(0x100, 0x78) for the 512-dot display mode, the same
    // pair Spyro 2 states, so the guest's authored view is 512 columns wide.
    .nativeWidth = 512,
    // The passes' own tables. Each Spyro 2 address is the one its use in game/render/
    // guest_terrain_*.cpp names, and each Spyro 3 address was read out of THIS image at the
    // offset-aligned counterpart instruction -- which is exact rather than inferred, because the
    // two drawers are instruction-identical, so instruction N of one is instruction N of the other.
    // The static data region moves by a constant +0x3CC8 between the two images, which every one of
    // these sites confirms and which is why no address below was derived by arithmetic alone.
    .classify =
        {
            .sectorTable = 0x8006D048u,  // 80023140
            .sectorCount = 0x8006D04Cu,  // 80023144
            .viewRotation = 0x8006E00Cu, // 800223A8: five RT words, camera-relative
            .animationTables = {0x8006E364u, 0x8006E36Cu, 0x8006E374u, 0x8006E37Cu},
        },
    .detail =
        {
            .textureTable = 0x8006D068u, // 80022D64
            .fogLevel = 0x8006E038u,     // 80022978: IR0 for the fog fade, 0 = none
        },
    .far =
        {
            .farDepth = 0x8006D074u, // 800272E0
        },
    .coarse =
        {
            .quadCells = 0x80065C28u,          // 80024BFC
            .quadCracks = 0x80065C78u,         // 80024AB4
            .pageRemaps = 0x80065D08u,         // 80024D28
            .triangleCracks = 0x80065DA8u,     // 8002514C
            .triangleCells = 0x80065D58u,      // 80025294
            .triangleSubTexture = 0x80065F58u, // 800252A0
            .triangleUvs = 0x80066028u,        // 800252A8
        },
    .fine =
        {
            .quadPieces = 0x80065C38u,     // 80025F20
            .quadCracks = 0x80065CA8u,     // 80025DCC
            .innerPoints = 0x80065D48u,    // 80025CD8
            .pageRemaps = 0x80065D08u,     // 80025FE8, the same table the coarse pass uses
            .trianglePieces = 0x80065D68u, // 80026A0C
            .triangleCracks = 0x80065E38u, // 800268B8
            .pieceTexture = 0x80065F68u,   // 80026A18
            .pieceShift = 0x80065FA8u,     // 80026A88
            .triangleUvs = 0x80066128u,    // 80026A20
        },
    .resplit =
        {
            .trianglePieces = 0x80066228u, // 80027138
            .quadPieces = 0x80066244u,     // 80026F6C
        },
};

// The moby visibility walk, 0x80030478.
//
//   800304C0  the level's moby array, at 0x8006C550; the records are 0x58 bytes and the walk starts
//             at the LAST one (`addi $ra,$ra,-0x58` at 8003053C).
//   800304CC  the drawer parameter parked in VZ1, at 0x8006C5DC.
//   80030508  the per-mesh slot array table, at 0x8006EE2C.
//   80030B2C  the overlay byte per record kind, at 0x80066530.
//   80030CF0  v0 on return is 0x8006E388 (`lui $v0,0x8007 ; addiu $v0,$v0,-0x1c78`); 80030CF8
//             stores the close-list cursor at 0x8006E390, eight bytes past it.
//   800307BC  the sine table, at 0x800658A0; the cosine table is the same table a quarter turn on,
//             at 0x80065920, which is what this image states.
inline constexpr spyro::guest_moby::Facts kMobyFacts{
    .entry = 0x80030478u,
    .overrideName = "spyro3-moby-visibility",
    .firstRecordWord = 0x8006C550u,
    .drawerParameter = 0x8006C5DCu,
    .meshSlotTable = 0x8006EE2Cu,
    .overlayTable = 0x80066530u,
    .closeCursorHome = 0x8006E390u,
    .exitV0 = 0x8006E388u,
    .sineTable = 0x800658A0u,
};

// The two override bodies: one line each, because an override is a plain `void (*)(Core *)` with
// nowhere to hang a back-pointer, so the title's facts are bound here and the routine they name is
// the shared one.
inline void terrainDrawerOverride(Core *core) {
  spyro::guest_terrain::draw(*core, kTerrainFacts, kRenderGlobals);
}

inline void mobyVisibilityOverride(Core *core) {
  spyro::guest_moby::walk(*core, kMobyFacts, kRenderGlobals);
}

// Install both overrides for this Core's resident SCUS_944.67 image.
inline void registerRenderOverrides(Core &core) {
  spyro::installNativeOverride(
      core, kTerrainFacts.entry, kTerrainFacts.overrideName, terrainDrawerOverride);
  spyro::installNativeOverride(
      core, kMobyFacts.entry, kMobyFacts.overrideName, mobyVisibilityOverride);
}

} // namespace spyro3
