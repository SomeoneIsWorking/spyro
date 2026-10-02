// spyro2_render_facts.h — SCUS_944.25's own render-routine facts, read out of its bytes, plus the
// two native overrides that bind them.
//
// The routines themselves live in game/render/ and are shared with SCUS_944.67: the two images'
// terrain drawer and moby visibility walk decompile to the same C with only their addresses (and
// one `gp` spill) different. Everything here is what THIS image says, so a reader can re-derive
// each address rather than trust a table, and so the shared routines carry no title's addresses at
// all.
#pragma once

#include "core.h"
#include "guest_moby_visibility.h"
#include "guest_render_globals.h"
#include "guest_terrain_drawer.h"
#include "guest_terrain_facts.h"
#include "native_execution.h"

namespace spyro2 {

// The guest globals both routines share, each named by the instruction that reaches it.
//
//   8006A9EC  the register save area: the walk's prologue (80043858..8004388C) and the terrain
//             drawer's (80023BB4..80023BD8) both spill s0..s7, gp, sp, fp, ra here, twelve words
//             at four bytes apart, and both reload the same words on exit.
//   80067034  the frame's scratch block end. The walk reads it at 80043894 and derives every list
//             base from it; the drawer derives its sector-list base from it at 80023BF4.
//   80067030  next free primitive packet, written by the drawer's exit (80029118).
//   80066FFC  the ordering table, whose 8-byte depth bins both routines link into.
//   80067168  the far-shade watermark, lowered only by the walk (80044030..80044050).
//   80067E84  the camera rotation, five RT words, read at 800438F8.
//   80067EAC  the camera position x, y, z, read at 800438FC.
//   8006B300  one visibility byte per terrain sector, written by the classification pass (800244F0)
//             and read by the walk for each moby's group byte.
inline constexpr spyro::guest_render_globals::Globals kRenderGlobals{
    .registerSaveArea = 0x8006A9ECu,
    .scratchBaseWord = 0x80067034u,
    .primitiveCursor = 0x80067030u,
    .orderingTable = 0x80066FFCu,
    .orderingTableMark = 0x80067168u,
    .cameraRotation = 0x80067E84u,
    .cameraPosition = 0x80067EACu,
    .visibilityGroups = 0x8006B300u,
};

// The terrain drawer, 0x80023BB4.
//
//   80023C04  jal 0x80048D18      the drawer's only call out of its own body
//   80023C14  sw  $v0, ...        its v0 lands in the visible-sector count word 0x80067404
//   80023BF4  the scratch-list base: [0x80067034] - 0x3000
//
// The passes' own tables (the classification pass's sector table and count, the texture table, the
// fog level and colour words, and the split passes' cell/crack/UV tables) are named at their use in
// game/render/guest_terrain_*.cpp with the instruction that reaches them; those tables live in the
// same static data region in both images of this family and their addresses are quoted there per
// image.
inline constexpr spyro::guest_terrain::Facts kTerrainFacts{
    .entry = 0x80023BB4u,
    .overrideName = "spyro2-terrain-drawer",
    .visibilityCallSite = 0x80023C04u,
    .sectorVisibility = 0x80048D18u,
    .visibleSectorCount = 0x80067404u,
    .scratchListsBelowEnd = 0x3000u,
    .fogColour = 0x80067408u,     // RFC, GFC, BFC words
    .foggedColours = 0x800683F0u, // the faded copy of a sector's colours
    // The display bootstrap's own geometry init states SetGeomOffset(0x100, 0x78) for the 512-dot
    // display mode, so the guest's authored view is 512 columns wide.
    .nativeWidth = 512,
    // The passes' own tables, each named at its use in game/render/guest_terrain_*.cpp with the
    // instruction here that reaches it.
    .classify =
        {
            .sectorTable = 0x800673D4u,  // 8002497C
            .sectorCount = 0x800673D8u,  // 80024980
            .viewRotation = 0x80067E98u, // 80023C20: five RT words, camera-relative
            .animationTables = {0x800681E8u, 0x800681F0u, 0x800681F8u, 0x80068200u},
        },
    .detail =
        {
            .textureTable = 0x800673F4u, // 800245A0
            .fogLevel = 0x80067EC4u,     // 80024550: IR0 for the fog fade, 0 = none
        },
    .far =
        {
            .farDepth = 0x80067400u, // 80028B1C
        },
    .coarse =
        {
            .quadCells = 0x80061F60u,          // 80026438
            .quadCracks = 0x80061FB0u,         // 800262F0
            .pageRemaps = 0x80062040u,         // 80026564
            .triangleCracks = 0x800620E0u,     // 80026988
            .triangleCells = 0x80062090u,      // 80026AD0
            .triangleSubTexture = 0x80062290u, // 80026ADC
            .triangleUvs = 0x80062360u,        // 80026AE4
        },
    .fine =
        {
            .quadPieces = 0x80061F70u,     // 8002775C
            .quadCracks = 0x80061FE0u,     // 80027608
            .innerPoints = 0x80062080u,    // 80027514
            .pageRemaps = 0x80062040u,     // 80027824, the same table the coarse pass uses
            .trianglePieces = 0x800620A0u, // 80028248
            .triangleCracks = 0x80062170u, // 800280F4
            .pieceTexture = 0x800622A0u,   // 80028254
            .pieceShift = 0x800622E0u,     // 800282C4
            .triangleUvs = 0x80062460u,    // 8002825C
        },
    .resplit =
        {
            .trianglePieces = 0x80062560u, // 80028974
            .quadPieces = 0x8006257Cu,     // 800287A8
        },
};

// The moby visibility walk, 0x80043858.
//
//   800438A4  the level's moby array, at 0x80066F14; the records are 0x58 bytes and the walk starts
//             at the LAST one.
//   800438B0  the drawer parameter parked in VZ1, at 0x80066FA4.
//   800438E4  the per-mesh slot array table, at 0x80068C94.
//   80043F0C  the overlay byte per record kind, at 0x80062868.
//   800440D0  v0 on return is 0x8006820C; 800440D8 stores the close-list cursor at 0x80068214.
//   80043B9C  the sine table, at 0x80061BD8; the cosine table is the same table a quarter turn on.
inline constexpr spyro::guest_moby::Facts kMobyFacts{
    .entry = 0x80043858u,
    .overrideName = "spyro2-moby-visibility",
    .firstRecordWord = 0x80066F14u,
    .drawerParameter = 0x80066FA4u,
    .meshSlotTable = 0x80068C94u,
    .overlayTable = 0x80062868u,
    .closeCursorHome = 0x80068214u,
    .exitV0 = 0x8006820Cu,
    .sineTable = 0x80061BD8u,
};

// The two override bodies. They are one line each because an override is a plain
// `void (*)(Core *)` with nowhere to hang a back-pointer: the title's facts are bound here and the
// routine they name is the shared one.
inline void terrainDrawerOverride(Core *core) {
  spyro::guest_terrain::draw(*core, kTerrainFacts, kRenderGlobals);
}

inline void mobyVisibilityOverride(Core *core) {
  spyro::guest_moby::walk(*core, kMobyFacts, kRenderGlobals);
}

// Install both overrides for this Core's resident SCUS_944.25 image.
inline void registerRenderOverrides(Core &core) {
  spyro::installNativeOverride(
      core, kTerrainFacts.entry, kTerrainFacts.overrideName, terrainDrawerOverride);
  spyro::installNativeOverride(
      core, kMobyFacts.entry, kMobyFacts.overrideName, mobyVisibilityOverride);
}

} // namespace spyro2
