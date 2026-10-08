#pragma once

#include <array>
#include <cstdint>

class Core;

// sector_visibility — RenderWorldChunks' per-sector broad-visibility table, as TWO answers.
//
// The guest renderer 0x800258F0 writes one byte per world sector into D_800771C8. The guest reads
// it back: 0x800521C0 drops every Moby whose category (sector) byte is zero from next frame's draw
// list and clears its +0x51 "was drawn" byte, and 0x80051FEC's update-list builder gates on that
// byte. So the table is gameplay state, and widescreen must never write a widened answer into it
// (issue 0152).
//
// The port's own readers — the regular Moby pass's category filter and the cyclorama's portal
// test — decide what the port DRAWS, and those must see the sectors the widened view admits. The
// two answers are produced together by one preparation and published together by one submission,
// so the drawn table always describes the same frame as the guest table beside it.
namespace spyro::sector_visibility {

inline constexpr uint32_t kGuestTable = 0x800771C8u;
inline constexpr uint32_t kSectors = 256u;

using Table = std::array<uint8_t, kSectors>;

struct Split {
  Table guest{}; // retail's 4:3 answer; the only one written into guest RAM
  Table drawn{}; // what the port draws; a superset of `guest`, and equal to it at 4:3

  friend bool operator==(const Split &, const Split &) = default;
};

// 0x800521C0's membership rule: category 0xFF is always admitted, otherwise its sector byte
// decides.
constexpr bool categoryVisible(const Table &table, uint32_t category) {
  return category == 0xffu || table[category & 0xffu] > 0u;
}

Table readGuest(Core &core);
void publishGuest(Core &core, const Table &table);

} // namespace spyro::sector_visibility
