#pragma once

#include <cstdint>

// SCUS_942.28's ACTOR POOL: the guest's linked list of moby records, walked from `cursor` back to
// `end`. Every actor a native producer draws is a record in it, and several producers read or move
// the cursor — the screen-sprite queue drains it, the HUD text builder allocates from it, and the
// frame's own environment resets it between fields.
//
// Those four sites used to each carry the literal. A cursor that means "the actor pool" in one
// producer and "a number that happened to be the actor pool" in another is a fact waiting to drift,
// so it is stated once here.
namespace spyro::guest_actor_pool {

// The allocation cursor: the next free record. Records are allocated DOWNWARD, so the records in
// use are the ones at addresses BELOW it.
inline constexpr std::uint32_t kCursorAddress = 0x80075710u;

// The end of the list: the first record that is not in use. Records below this belong to the frame.
inline constexpr std::uint32_t kEndAddress = 0x800756FCu;

// One moby record, as the guest's own code walks the pool. Producers that step the cursor by one
// record step it by this, never by a local guess.
inline constexpr std::uint32_t kRecordSize = 0x58u;

} // namespace spyro::guest_actor_pool
