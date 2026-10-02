// guest_render_globals.h — the guest globals the hand-written render routines of this engine family
// share, as a per-title fact table.
//
// The moby visibility walk and the terrain drawer are both hand-written assembly that borrow every
// general register, so both spill the callee-saved set (and gp, sp, fp, ra) to the SAME save area
// on entry and reload it on exit. They also share the frame's scratch block, the camera, the
// ordering table the drawn primitives are linked into, and the per-sector visibility bytes.
//
// Every address here is a property of ONE image, so it arrives as data rather than as a constant
// baked into the routine that reads it. That is what lets two titles of this family share the
// routines themselves: the arithmetic and the traversal are one implementation, and the addresses
// it touches are stated once per title in that title's own facts header.
#pragma once

#include "core.h"

#include <array>
#include <cstdint>

namespace spyro::guest_render_globals {

struct Globals {
  std::uint32_t registerSaveArea = 0;  // s0..s7, gp, sp, fp, ra
  std::uint32_t scratchBaseWord = 0;   // the frame's scratch block end
  std::uint32_t primitiveCursor = 0;   // next free primitive packet
  std::uint32_t orderingTable = 0;     // 8-byte depth bins: last packet linked, first
  std::uint32_t orderingTableMark = 0; // deepest bin a far prim reached
  std::uint32_t cameraRotation = 0;    // five RT words
  std::uint32_t cameraPosition = 0;    // x, y, z words
  std::uint32_t visibilityGroups = 0;  // one byte per terrain sector
};

// The register numbers retail saves, in save-area order. Measured, not assumed: the prologues of
// the two routines in this family spill exactly these twelve words at 4 bytes apart.
inline constexpr std::array<std::uint32_t, 12> kSavedRegisters = {
    16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};

// Retail's prologue: spill the borrowed registers. Neither routine changes them in its native form,
// so the matching epilogue reloads exactly these values and needs no native counterpart.
inline void spillBorrowedRegisters(Core &core, const Globals &globals) {
  for (std::size_t i = 0; i < kSavedRegisters.size(); ++i) {
    core.mem_w32(globals.registerSaveArea + static_cast<std::uint32_t>(4 * i),
                 core.r[kSavedRegisters[i]]);
  }
}

} // namespace spyro::guest_render_globals
