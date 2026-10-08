#pragma once

#include <cstdint>

class Core;

// Stage 13, mode 3: the save picker's three-slot caption, laid into the actor pool as one text moby
// per character for the screen-sprite queue to draw.
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

// Lay the caption run into the actor pool if this frame calls for one. `originalPool` is the cursor
// BEFORE the run allocates, so the caller can tell which records it added.
bool present(Core *c, std::uint32_t originalPool);

} // namespace spyro::stage13_text_run
