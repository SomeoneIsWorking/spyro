// spyro2_terrain_frame.h — the state one call of Spyro 2's terrain drawer (SCUS_944.25 0x80023BB4)
// carries from pass to pass.
//
// The drawer is one hand-written routine that runs its passes back to back (see
// spyro2_terrain_passes.h). Retail threads a handful of values through all of them in registers it
// never spills: the next free primitive packet (fp), the ordering table, the scratch block the
// sector lists live in, and the screen window the outcodes test. This is that set, plus the two
// result registers: retail returns with v0 and v1 holding whatever its last pass left in them, and
// the O32 contract makes both observable, so each pass records what it leaves there.
#pragma once

#include "core.h"
#include "spyro2_terrain_screen.h"

#include <cstdint>

namespace spyro2::terrain {

// The sector lists, as offsets from the scratch block base (VXY1, 0x3000 below the frame's scratch
// block end). The classification pass (80023C9C) builds the first three; the two near passes defer
// polygons into the last two for subdivision.
inline constexpr std::uint32_t kDetailList = 0x2000; // textured sectors, near enough for detail
inline constexpr std::uint32_t kTranslucentList = 0x2200; // translucent sectors (flag 0x5000)
inline constexpr std::uint32_t kFineSplitList = 0x2300; // polygons with a vertex nearer than 0x140
inline constexpr std::uint32_t kCoarseSplitList = 0x2400; // polygons near enough to split once
inline constexpr std::uint32_t kFarList = 0x2A00;         // untextured low-detail sectors
// The fine pass reuses kCoarseSplitList, already walked, as the queue of primitives too large for
// the GPU (spyro2_terrain_resplit.cpp); kSplitPrimitiveCursor is its end.
inline constexpr std::uint32_t kResplitQueue = kCoarseSplitList;

// The drawer's scratchpad. Every pass reuses the same region for its vertex cache.
inline constexpr std::uint32_t kScratchpad = 0x1F800000u;
inline constexpr std::uint32_t kVertexDepths = 0x1F8002A8u; // the detail passes' SZ halfwords
inline constexpr std::uint32_t kSplitPrimitiveCursor =
    0x1F8003FCu; // one past kResplitQueue's last entry

struct TerrainFrame {
  TerrainFrame(Core &core, ScreenBounds bounds) : core(core), bounds(bounds) {}

  // Link the primitive at `primitive` as the new head of ordering-table bin `bin`, advance past its
  // `bytes` bytes, and return the bin's previous head (80024F5C, and the same five instructions in
  // every pass; which registers they leave the head and `primitive >> 16` in differs by pass).
  std::uint32_t linkAndAdvance(std::uint32_t bin, std::uint32_t bytes) {
    const std::uint32_t slot = orderingTable + bin * 8;
    const std::uint32_t head = core.mem_r32(slot);
    core.mem_w32(slot, primitive);
    if (head == 0) {
      core.mem_w32(slot + 4, primitive);
    } else {
      linkAfter(head, primitive);
    }
    primitive += bytes;
    return head;
  }

  // Make the packet at `packet` point at `next`: its low 24 bits, the tag's address field.
  void linkAfter(std::uint32_t packet, std::uint32_t next) {
    core.mem_w16(packet, static_cast<std::uint16_t>(next));
    core.mem_w8(packet + 2, static_cast<std::uint8_t>(next >> 16));
  }

  Core &core;
  const ScreenBounds bounds;
  std::uint32_t scratch = 0;           // the scratch block base, VXY1
  std::uint32_t primitive = 0;         // fp: the next free primitive packet
  std::uint32_t orderingTable = 0;     // the ordering table's bin 0
  std::uint32_t textures = 0;          // the level's 48-byte texture records
  std::uint32_t fineSplitCursor = 0;   // next free kFineSplitList word
  std::uint32_t coarseSplitCursor = 0; // next free kCoarseSplitList word
  std::uint32_t v0 = 0;
  std::uint32_t v1 = 0;
};

} // namespace spyro2::terrain
