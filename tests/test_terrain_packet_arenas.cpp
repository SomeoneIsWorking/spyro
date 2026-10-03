// test_terrain_packet_arenas.cpp — the terrain pass record, keyed by ARENA rather than by field.
//
// A temporal strategy owns the terrain in a captured queue by the guest's own packet addresses, and
// the record it matches against is whatever the drawer linked. Keying that record the obvious ways
// both fail, and the measurements are why:
//
//   ONE UNION OF EVERY PASS (spyro::context's `terrainPackets` was this) — the guest
//   DOUBLE-BUFFERS. MEASURED 2026-10-03 on SCUS_944.25: one terrain pass runs per scene tick into
//   one of two packet arenas, the primitive cursor alternating between 0x801A2984.. and
//   0x801C04BC.., and the queue presented on that tick carries the OTHER pass. A union describes
//   both, so it answers true for addresses the presented queue does not hold and the match, while
//   it matched 719 of 1355 items on the first frame, found 0 of 1355 on every frame after.
//
//   A TICK-COUNTED WINDOW — right answer, wrong derivation, and it fails the moment the cadence is
//   not the assumed one: two ticks held 1174/1174 on the first frames and then 613 addresses
//   against 0 hits.
//
// Keying by arena is right BY CONSTRUCTION: a pass is identified by the range it allocated into, so
// the record that contains an address is the pass that produced it. These tests are that claim,
// with the two-arena alternation the real double buffer produces.
#include "spyro_context.h"

#include "core.h"
#include "testutil.h"

#include <algorithm>
#include <vector>

namespace {

using ::spyro::Context;

constexpr std::uint32_t kArenaA = 0x801A2984u;
constexpr std::uint32_t kArenaB = 0x801C04BCu;

// What Drawer::run does for a real field: take the record for the arena at `begin`, then extend it
// with each address the pass links, exactly as `TerrainFrame::linkAndAdvance` does.
constexpr std::uint32_t kPacketWords = 4;

void linkPass(spyro::Context &context,
              std::uint32_t begin,
              const std::vector<std::uint32_t> &packets) {
  spyro::Context::TerrainPacketArena &arena = context.beginTerrainArena(begin);
  for (std::uint32_t packet : packets) {
    arena.packets.push_back(packet);
    arena.end = std::max(arena.end, packet + 4u * kPacketWords);
  }
  std::sort(arena.packets.begin(), arena.packets.end());
  arena.packets.erase(std::unique(arena.packets.begin(), arena.packets.end()), arena.packets.end());
}

// What `TerrainWorldPass::owns` does: the arena containing the address decides, and that arena's
// own linked set answers. Not a union, and not a search across arenas.
bool owns(spyro::Context &context, std::uint32_t guestPacket) {
  for (const spyro::Context::TerrainPacketArena &arena : context.terrainArenas) {
    if (guestPacket < arena.begin || guestPacket >= arena.end) {
      continue;
    }
    return std::binary_search(arena.packets.begin(), arena.packets.end(), guestPacket);
  }
  return false;
}

// One packet every 16 bytes, so a pass's packets are 16 bytes apart and the gaps between them are
// inside the record's range but NOT in its linked set — the two tests are not the same test.
std::vector<std::uint32_t> passIn(std::uint32_t begin, std::uint32_t count) {
  std::vector<std::uint32_t> packets;
  for (std::uint32_t i = 0; i < count; ++i) {
    packets.push_back(begin + i * 16u);
  }
  return packets;
}

} // namespace

// THE ALTERNATION: each pass writes its own arena, and each arena's record answers only for itself.
void test_each_arena_answers_only_for_its_own_pass() {
  spyro::Context context;
  const std::vector<std::uint32_t> passA = passIn(kArenaA, 8);
  const std::vector<std::uint32_t> passB = passIn(kArenaB, 8);
  linkPass(context, kArenaA, passA);
  linkPass(context, kArenaB, passB);
  CHECK_EQ(context.terrainArenas.size(), std::size_t{2});

  for (std::uint32_t packet : passA) {
    CHECK(owns(context, packet));
  }
  for (std::uint32_t packet : passB) {
    CHECK(owns(context, packet));
  }
  // The address BETWEEN the two arenas belongs to neither pass: no arena contains it, so no pass
  // produced it. A union search would have to walk every record to reach the same answer;
  // containment reaches it by refusing to look.
  CHECK(!owns(context, 0x801B0000u));
}

// THE ORDER DOES NOT MATTER: the presented queue alternates, so a record must survive the OTHER
// arena being written after it.
void test_a_pass_record_survives_the_other_arena_being_rewritten() {
  spyro::Context context;
  const std::vector<std::uint32_t> passA = passIn(kArenaA, 8);
  linkPass(context, kArenaA, passA);
  // Ten later passes into the other arena. If the records were one shared set, or if writing one
  // arena disturbed the other, the first arena's answers would be gone by now.
  for (int pass = 0; pass < 10; ++pass) {
    linkPass(context, kArenaB, passIn(kArenaB, 4));
  }
  for (std::uint32_t packet : passA) {
    CHECK(owns(context, packet));
  }
}

// REWRITING AN ARENA REPLACES ITS RECORD. A second pass into the same arena reuses those addresses,
// so the first pass's addresses are stale the moment the second writes them — and the record must
// not go on answering for them.
void test_rewriting_an_arena_replaces_that_arenas_record() {
  spyro::Context context;
  const std::vector<std::uint32_t> first = passIn(kArenaA, 16);
  linkPass(context, kArenaA, first);

  // The second pass allocates FURTHER than the first, so the first's addresses lie inside the new
  // range: containment selects this arena either way, and only the replacement set can answer.
  const std::vector<std::uint32_t> second = passIn(kArenaA + 16u * 16u, 8);
  linkPass(context, kArenaA, second);
  CHECK_EQ(context.terrainArenas.size(), std::size_t{1});
  for (std::uint32_t packet : second) {
    CHECK(owns(context, packet));
  }
  for (std::uint32_t packet : first) {
    CHECK(!owns(context, packet));
  }
}

// A PASS THAT LINKS NOTHING OWNS NOTHING. An arena is recorded by its allocation range, which for a
// pass that linked nothing is empty, so nothing inside it can be claimed.
void test_an_empty_pass_owns_nothing() {
  spyro::Context context;
  linkPass(context, kArenaA, {});
  CHECK(!owns(context, kArenaA));
  CHECK(!owns(context, kArenaA + 0x1000u));
}

// THE RECORD IS BOUNDED. Two arenas is what the real double buffer alternates, but the cursor could
// move somewhere new, and a context that grew a record per pass would grow without limit.
void test_the_arena_record_is_bounded() {
  spyro::Context context;
  for (std::uint32_t i = 0; i < spyro::Context::kMaxTerrainArenas * 2u; ++i) {
    linkPass(context, kArenaA + i * 0x10000u, passIn(kArenaA + i * 0x10000u, 4));
  }
  CHECK_EQ(context.terrainArenas.size(), spyro::Context::kMaxTerrainArenas);
  // The most recent arena is the one that survived; the oldest was dropped.
  const std::uint32_t newest = kArenaA + (spyro::Context::kMaxTerrainArenas * 2u - 1u) * 0x10000u;
  CHECK(owns(context, newest));
  CHECK(!owns(context, kArenaA));
}

int main() {
  RUN(each_arena_answers_only_for_its_own_pass);
  RUN(a_pass_record_survives_the_other_arena_being_rewritten);
  RUN(rewriting_an_arena_replaces_that_arenas_record);
  RUN(an_empty_pass_owns_nothing);
  RUN(the_arena_record_is_bounded);
  return pt_summary();
}
