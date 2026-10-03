// guest_terrain_facts.h — what ONE image says about its terrain drawer, as data.
//
// The drawer itself is one routine in this engine family (guest_terrain_drawer.cpp and its passes),
// measured instruction-for-instruction identical between SCUS_944.25 and SCUS_944.67: both are
// 5,487 instructions and their decompilations differ in exactly one line. Everything below is a
// property of the image the routine runs against, so it lives here and is supplied by the title's
// own facts header rather than baked into the passes: a routine that carried one title's addresses
// could not serve the other, and two copies of it could drift.
//
// The tables are grouped by the pass that reads them, because that is the only thing that says
// which of them a reader is looking at -- several of them are the same address in one image and a
// different one in another, and the split passes' crack tables in particular are two different
// tables.
#pragma once

#include <array>
#include <cstdint>

namespace spyro::guest_terrain {

// The classification pass's own guest words.
struct ClassifyFacts {
  std::uint32_t sectorTable = 0; // one pointer per terrain sector
  std::uint32_t sectorCount = 0;
  std::uint32_t viewRotation = 0; // five RT words, camera-relative
  // The animation tables, one per word slot.
  std::array<std::uint32_t, 4> animationTables{};
};

// The detail pass's own guest words.
struct DetailFacts {
  std::uint32_t textureTable = 0; // the level's 48-byte texture records
  std::uint32_t fogLevel = 0;     // IR0 for the fog fade, 0 = none
};

// The far pass's own guest word.
struct FarFacts {
  std::uint32_t farDepth = 0; // depth past which nothing is drawn
};

// The coarse split pass's tables, all in the executable's data.
struct CoarseFacts {
  std::uint32_t quadCells = 0;          // the four sub-quads' first cells
  std::uint32_t quadCracks = 0;         // one crack triangle per edge
  std::uint32_t pageRemaps = 0;         // UV remaps by page bits 28..30
  std::uint32_t triangleCracks = 0;     // 3 per dropped corner
  std::uint32_t triangleCells = 0;      // the four sub-triangles' cells
  std::uint32_t triangleSubTexture = 0; // signed byte offsets
  std::uint32_t triangleUvs = 0;        // UV deltas, 0x40 per corner
};

// The fine split pass's tables. `quadCracks` and `triangleCracks` here are NOT the coarse pass's
// tables of the same name: this pass puts two triangles per edge where the coarse pass puts one.
struct FineFacts {
  std::uint32_t quadPieces = 0;     // 16 first-cell words | quadrant
  std::uint32_t quadCracks = 0;     // two triangles per edge
  std::uint32_t innerPoints = 0;    // the 4 points refined
  std::uint32_t pageRemaps = 0;     // the same remap table the coarse pass uses
  std::uint32_t trianglePieces = 0; // 16 cell-triple words
  std::uint32_t triangleCracks = 0; // 0x48 per dropped corner
  std::uint32_t pieceTexture = 0;   // signed bytes
  std::uint32_t pieceShift = 0;     // UV/page shifts
  std::uint32_t triangleUvs = 0;    // UV deltas, 0x40 per corner
};

// The GPU-size re-split pass's tables. They are a third pair, distinct from both split passes'.
struct ResplitFacts {
  std::uint32_t trianglePieces = 0; // 7 words: three cell offsets, plus bit 0 "never too large"
  std::uint32_t quadPieces = 0;     // 12 words
};

struct Facts {
  // The drawer's own entry, the address the native override is installed at.
  std::uint32_t entry = 0;
  // The name the framework's override differential and its logs identify this body by.
  const char *overrideName = "";

  // The drawer's one guest call out of its own body: the sector-visibility leaf, and the address of
  // the `jal` it executes to reach it. The call site is a separate fact because a nested guest call
  // runs with the `$ra` that site's `jal` leaves, and the differential compares `$ra` at the
  // return.
  std::uint32_t visibilityCallSite = 0;
  std::uint32_t sectorVisibility = 0;
  // The guest word the call's v0 lands in, which the classification pass then walks.
  std::uint32_t visibleSectorCount = 0;

  // How far below the frame's scratch block end the drawer's sector lists start.
  std::uint32_t scratchListsBelowEnd = 0;

  // The level's fog colour (RFC, GFC, BFC words) and the shared buffer a fogged sector's colours
  // are faded into before its polygons read them.
  std::uint32_t fogColour = 0;
  std::uint32_t foggedColours = 0;

  // The authored horizontal window this title's own display bootstrap states, in columns.
  std::int32_t nativeWidth = 0;

  ClassifyFacts classify{};
  DetailFacts detail{};
  FarFacts far{};
  CoarseFacts coarse{};
  FineFacts fine{};
  ResplitFacts resplit{};
};

} // namespace spyro::guest_terrain
