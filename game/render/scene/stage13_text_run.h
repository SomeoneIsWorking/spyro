#pragma once

#include <cstdint>

class Core;

// SCUS_942.28 stage 13, mode 3: the save picker's three-slot caption. While the picker's state is
// 2 and its timer is past 0x8B, the guest lays a run of TEXT MOBYS into the actor pool above the
// caption line — one per character, positioned from the run's own first string address, coloured
// by a per-character sine table once the run stops growing — and the screen-sprite queue then draws
// whatever is in the pool.
//
// This owner exists because that is one behaviour with two halves: which caption and how far along
// it is, and how a string becomes a row of mobys. They were in the queue's source file only because
// the queue is what finally draws them, which made the queue look like it also authored the game's
// captions.
namespace spyro::stage13_text_run {

// The picker's state word. 2 is the caption run; every other value draws no caption.
inline constexpr std::uint32_t kStateAddress = 0x80078D7Cu;

// The picker's timer. The run begins past 0x8B and each caption has its own stop timer, reached
// partway through its own string.
inline constexpr std::uint32_t kTimerAddress = 0x80078D80u;

// Which caption is showing: 0 the "new game" line, 1 the "continue" line (which the continue flag
// splits into two different strings), anything else the tally line.
inline constexpr std::uint32_t kTextAddress = 0x80078D94u;

// Zero once the player has answered the continue prompt.
inline constexpr std::uint32_t kContinueFlagAddress = 0x80078E78u;

// The character-colour table, indexed by an angle the run derives from its timer.
inline constexpr std::uint32_t kShadeTableAddress = 0x8006CC78u;

// Lay the caption run into the actor pool if this frame calls for one. `originalPool` is the
// cursor BEFORE the run allocates, so the caller can tell which records the run added. Returns
// whether a run was laid.
bool present(Core *c, std::uint32_t originalPool);

} // namespace spyro::stage13_text_run
