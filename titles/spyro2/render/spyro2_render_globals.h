// spyro2_render_globals.h — guest globals Spyro 2's hand-written render routines share, named once.
//
// The moby visibility walk (0x80043858) and the terrain drawer (0x80023BB4) are both hand-written
// assembly that borrow every general register, so both spill the callee-saved set (and gp, sp, fp,
// ra) to the SAME save area on entry and reload it on exit. They also share the frame's scratch
// block, the camera, and the ordering table the drawn primitives are linked into.
#pragma once

#include "core.h"

#include <array>
#include <cstdint>

namespace spyro2::render_globals {

inline constexpr std::uint32_t kRegisterSaveArea = 0x8006A9ECu; // s0..s7, gp, sp, fp, ra
inline constexpr std::array<std::uint32_t, 12> kSavedRegisters = {
    16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};

inline constexpr std::uint32_t kScratchBaseWord = 0x80067034u; // the frame's scratch block end
inline constexpr std::uint32_t kPrimitiveCursor = 0x80067030u; // next free primitive packet
inline constexpr std::uint32_t kOrderingTable =
    0x80066FFCu; // 8-byte depth bins: last packet linked, first
inline constexpr std::uint32_t kOrderingTableMark = 0x80067168u; // deepest bin a far prim reached
inline constexpr std::uint32_t kCameraRotation = 0x80067E84u;    // five RT words
inline constexpr std::uint32_t kCameraPosition = 0x80067EACu;    // x, y, z words
inline constexpr std::uint32_t kVisibilityGroups = 0x8006B300u;  // one byte per terrain sector

// Retail's prologue: spill the borrowed registers. Neither routine changes them in its native form,
// so the matching epilogue reloads exactly these values and needs no native counterpart.
inline void spillBorrowedRegisters(Core &core) {
  for (std::size_t i = 0; i < kSavedRegisters.size(); ++i) {
    core.mem_w32(kRegisterSaveArea + static_cast<std::uint32_t>(4 * i), core.r[kSavedRegisters[i]]);
  }
}

} // namespace spyro2::render_globals
