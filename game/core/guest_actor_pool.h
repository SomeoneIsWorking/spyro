#pragma once

#include <cstdint>

// SCUS_942.28's ACTOR POOL: the guest's linked list of moby records, walked from `cursor` back to
// `end`. Native producers draw records from it, and the screen-sprite queue, HUD text builder and
// the frame's environment reset all move the cursor.
namespace spyro::guest_actor_pool {

// The next free record; records are allocated DOWNWARD, so the ones in use are below it.
inline constexpr std::uint32_t kCursorAddress = 0x80075710u;

// The first record not in use; records below this belong to the frame.
inline constexpr std::uint32_t kEndAddress = 0x800756FCu;

// One moby record, as the guest's own code walks the pool.
inline constexpr std::uint32_t kRecordSize = 0x58u;

} // namespace spyro::guest_actor_pool
