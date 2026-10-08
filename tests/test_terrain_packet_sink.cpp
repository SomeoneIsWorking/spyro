// test_terrain_packet_sink.cpp — retail's flatten order, proven against the walk that submits.
//
// THE ORDER IS NOT A CHOICE. SCUS_944.25's DrawOTag is preceded by a routine at 0x8001B2A8 that
// concatenates the ordering table's 512 depth bins into ONE chain before submitting, and this sink
// submits the in-between's terrain in exactly that order (terrain_packet_sink.h carries the
// disassembly-derived argument). Two of its consequences are invisible in a picture and fatal in a
// frame: the bins come out HIGHEST FIRST, so far terrain is submitted before near terrain and the
// nearer packet paints over it, and a bin's packets come out in LINK order — which is REVERSE
// allocation, because the drawer pushes each new packet at the bin's head.
//
// So these assert an order, a sequence of addresses, not a set. A sink that submitted the same
// packets in a different order would still draw something, and would still be wrong.
#include "terrain_packet_sink.h"

#include "core.h"
#include "testutil.h" // CHECK / RUN / pt_summary

#include <cstdint>
#include <vector>

namespace {

using spyro::guest_terrain::HostMemory;
using spyro::guest_terrain::TerrainMemory;
using spyro::terrain_packet_sink::Bound;
namespace sink = spyro::terrain_packet_sink;

// A table and an arena over ordinary guest-shaped addresses. They are the sink's INPUTS, not part
// of it: every address here is this test's, because the sink must not care where a table lives.
constexpr std::uint32_t kTableBase = 0x80020000u;
constexpr std::uint32_t kArenaBase = 0x80030000u;
constexpr std::uint32_t kArenaBytes = 0x4000u;

std::uint32_t slot(std::uint32_t bin) {
  return kTableBase + bin * sink::kBinStride;
}

// A packet: a tag word carrying its GP0 word count and its chain's next address, then that many
// words after it. The tag is the only place the count lives, which is why the sink reads it rather
// than assuming a packet size, and the next address is in its low 24 bits exactly as
// `TerrainFrame::linkAfter` writes.
void writePacket(TerrainMemory &memory,
                 std::uint32_t packet,
                 std::uint32_t next,
                 std::uint32_t count) {
  memory.w32(packet, (count << 24) | (next & 0x00FFFFFFu));
  for (std::uint32_t i = 1; i <= count; ++i) {
    memory.w32(packet + 4 * i, 0x0A000000u | i);
  }
}

std::vector<std::uint32_t> walk(TerrainMemory &memory, const Bound &bound) {
  std::vector<std::uint32_t> order;
  sink::forEachPacket(
      memory, bound, [&order](std::uint32_t, std::uint32_t packet, std::span<const std::uint32_t>) {
        order.push_back(packet);
      });
  return order;
}

} // namespace

void test_flatten_order() {
  Core core;
  HostMemory memory(core);
  memory.mapEmpty(kTableBase, sink::kTableBytes);
  memory.mapEmpty(kArenaBase, kArenaBytes);

  // ---- BIN ORDER: HIGHEST FIRST, AND WITHIN A BIN, LINK ORDER.
  // --------------------------------------
  //
  // Bin 0 gets two packets so the second half of the claim is observable: the drawer points the
  // old head at each new packet and makes it the head, so the chain starts at the bin's second word
  // (the first packet linked) and runs first -> second. A walk that started at the head would see
  // only the second packet.
  const std::uint32_t deepest = 40;
  const std::uint32_t middle = 7;
  const std::uint32_t lowest = 0;
  const std::uint32_t lowestFirst = kArenaBase + lowest * 0x40u;
  const std::uint32_t lowestSecond = lowestFirst + 0x30u;
  std::vector<std::uint8_t> linked(sink::kBins, 0);

  // Linked the way the drawer links: `lowestFirst` is linked, then `lowestSecond` after it and
  // becomes the head.
  memory.w32(slot(lowest), lowestSecond);
  memory.w32(slot(lowest) + 4, lowestFirst);
  writePacket(memory, lowestSecond, 0, 8);
  writePacket(memory, lowestFirst, lowestSecond, 8);
  linked[lowest] = 1;
  for (std::uint32_t bin : {middle, deepest}) {
    const std::uint32_t packet = kArenaBase + bin * 0x40u;
    memory.w32(slot(bin), packet);
    memory.w32(slot(bin) + 4, packet);
    writePacket(memory, packet, 0, 8);
    linked[bin] = 1;
  }
  // Bin 5 sits between the others and is NOT in the linked record, while its two table words still
  // name a real packet. A walk that read the table's memory instead of the record would submit it
  // here.
  memory.w32(slot(5), lowestFirst);
  memory.w32(slot(5) + 4, lowestFirst);

  const std::vector<std::uint32_t> order = walk(memory,
                                                Bound{
                                                    .tableBase = kTableBase,
                                                    .arenaBase = kArenaBase,
                                                    .arenaBytes = kArenaBytes,
                                                    .linkedBins = &linked,
                                                });
  const std::vector<std::uint32_t> want{
      kArenaBase + deepest * 0x40u, kArenaBase + middle * 0x40u, lowestFirst, lowestSecond};
  CHECK(order == want);

  // ---- AN UNLINKED BIN IS EMPTY, WHATEVER THE TABLE'S MEMORY HOLDS.
  // --------------------------------
  //
  // The table's bytes still carry the LAST REAL FIELD's chains wherever this traversal did not
  // write, because the in-between copies the guest's bytes rather than blanking them. A walk that
  // read memory instead of the linked-bin record would submit the guest's previous terrain as if it
  // were this camera's — which is why the record, and not the bytes, is what says a bin is empty.
  linked.assign(sink::kBins, 0);
  linked[9] = 1;
  const std::uint32_t mine = kArenaBase + 0x200u;
  memory.w32(slot(9), 0);
  memory.w32(slot(9) + 4, 0);
  memory.w32(slot(9), mine);
  memory.w32(slot(9) + 4, mine);
  writePacket(memory, mine, 0, 8);

  const std::vector<std::uint32_t> onlyMine = walk(memory,
                                                   Bound{
                                                       .tableBase = kTableBase,
                                                       .arenaBase = kArenaBase,
                                                       .arenaBytes = kArenaBytes,
                                                       .linkedBins = &linked,
                                                   });
  CHECK(onlyMine == std::vector<std::uint32_t>{mine});

  // ---- A BIN THE WALK NEVER REACHES IS NOT WALKED DOWN TO.
  // -----------------------------------------
  //
  // The linked-bin record is sized to the highest bin named, so the walk starts at that bin rather
  // than at bin 512. Here the record names bin 9 while the table has stale content in bin 400.
  memory.w32(slot(400), lowestFirst);
  memory.w32(slot(400) + 4, lowestFirst);
  const std::vector<std::uint32_t> fromNine = walk(memory,
                                                   Bound{
                                                       .tableBase = kTableBase,
                                                       .arenaBase = kArenaBase,
                                                       .arenaBytes = kArenaBytes,
                                                       .linkedBins = &linked,
                                                   });
  CHECK(fromNine == std::vector<std::uint32_t>{mine});
}

// The re-split pass empties an oversized primitive in place (tag length 0) and chains its pieces
// after it; the GPU walks through the empty one.
void test_an_emptied_primitive_is_walked_through() {
  Core core;
  HostMemory memory(core);
  memory.mapEmpty(kTableBase, sink::kTableBytes);
  memory.mapEmpty(kArenaBase, kArenaBytes);
  std::vector<std::uint8_t> linked(sink::kBins, 0);
  const std::uint32_t emptied = kArenaBase;
  const std::uint32_t piece = kArenaBase + 0x40u;
  const std::uint32_t after = kArenaBase + 0x80u;
  memory.w32(slot(3), after);
  memory.w32(slot(3) + 4, emptied);
  writePacket(memory, emptied, piece, 0);
  writePacket(memory, piece, after, 6);
  writePacket(memory, after, 0, 8);
  linked[3] = 1;
  const std::vector<std::uint32_t> order = walk(memory,
                                                Bound{
                                                    .tableBase = kTableBase,
                                                    .arenaBase = kArenaBase,
                                                    .arenaBytes = kArenaBytes,
                                                    .linkedBins = &linked,
                                                });
  CHECK(order == (std::vector<std::uint32_t>{piece, after}));
}

int main() {
  RUN(flatten_order);
  RUN(an_emptied_primitive_is_walked_through);
  return pt_summary();
}
