// terrain_packet_sink.h — where an in-between field's reconstructed terrain packets go.
//
// The in-between is not a second renderer and it does not know what a RenderQueue item is. It runs
// the guest's own terrain drawer over host memory, and the drawer does what retail's did: write GP0
// packets into an arena and link them into a 512-bin ordering table. So the in-between needs the
// same two objects the guest had, at the same guest-shaped addresses — and then something to walk
// them and hand the packets to the framework.
//
// THE WALK IS RETAIL'S, NOT A CHOICE. Measured from SCUS_944.25's own flatten routine (0x8001B2A8,
// the last thing its DrawOTag does before submitting):
//
//   it is called with bin 0x580 = 1408, past the table's 512 bins, so its start clamp never applies
//   and it begins at the table's own end; it scans DOWN from there for the first bin with a
//   non-zero head, then for each such bin links that bin's HEAD's next-pointer at the next-lower
//   non-empty bin's LAST entry, and finally points the lowest head at 0x8006012C, whose zero header
//   ends the chain.
//
//   so the single chain DrawOTag walks is the DEEPEST non-empty bin's packets first, then each
//   lower bin in turn down to bin 0, and within a bin the packets in link order. Its writes to the
//   table itself are bookkeeping — it clears each bin pair as it consumes it — and carry no meaning
//   after.
//
// Two consequences are load-bearing and not obvious. The depth bins come out HIGHEST FIRST, so far
// terrain is submitted before near terrain and the nearer packet paints over it, which is the whole
// reason the guest's ordering table exists. And within a bin the order is link order, and the
// drawer pushes at the head, so a bin's LAST linked packet is its first drawn — again retail's, not
// a sort imposed here. This walk imposes nothing.
//
// Every bound here is a refusal by name rather than a clamp. The arena is host memory, so an
// overrun would not corrupt the guest — but a silent overrun is how a plausible wrong picture
// starts, and the only thing a wrong picture is good for is hiding the fault that made it.
#pragma once

#include "guest_terrain_memory.h"
#include "ordering_table.h" // guestAddressOf, mainRamOffsetOf

#include <cstdint>
#include <cstdlib>
#include <lucent/log.h>
#include <vector>

class Core;

namespace spyro::terrain_packet_sink {

// 512 8-byte bins, the guest's own ordering-table layout. The drawer's mark arithmetic can name any
// of these bins, and further — see `Bound::linkedBins`.
inline constexpr std::uint32_t kBins = 512;
inline constexpr std::uint32_t kBinStride = 8;
inline constexpr std::uint32_t kTableBytes = kBins * kBinStride;

// The guest-shaped address the traversal builds its ordering table at, and the span its packet
// arena occupies. Both are the drawer's own, read from the guest's globals by the caller that runs
// it, so this header states no number of its own.
struct Bound {
  std::uint32_t tableBase = 0;
  std::uint32_t arenaBase = 0;
  std::uint32_t arenaBytes = 0;

  // The bins the traversal LINKED, one byte each (TerrainFrame::linkedBins). A bin outside this —
  // or inside it, holding zero — is empty, and the walk skips it.
  //
  // This is not an optimisation, and it is why the walk cannot read the table's memory to decide.
  // The table's bytes still hold the LAST REAL FIELD's chains wherever this traversal did not
  // write, because the in-between copies the guest's bytes rather than blanking them; and the bins
  // are not confined to 512 (MEASURED 2026-10-02 on SCUS_944.25: the far pass names bin 573, which
  // is where the guest's published mark lives). Walking the memory instead of this record would
  // submit the guest's previous field's terrain as if it were this camera's. Sizing the record by
  // the highest bin named also gives the walk its start, with no ceiling on how far above 512 the
  // far pass may reach.
  const std::vector<std::uint8_t> *linkedBins = nullptr;

  [[nodiscard]] bool valid() const {
    return tableBase != 0 && arenaBase != 0 && arenaBytes >= 8 && linkedBins != nullptr;
  }
};

namespace detail {

// A bin's eight bytes are a head and a last. The drawer writes both and retail's flatten reads both
// — it links each head at the next lower bin's LAST — so this walk reads the head, and the
// flatten's use of the last is already accounted for by the descending bin order below.
inline constexpr std::uint32_t kHeadOffset = 0;

// A linked packet's tag word: the count of GP0 words in the packet in its high byte, and the
// ADDRESS of the next packet in the chain in its low 24 bits. `TerrainFrame::linkAfter` writes
// exactly those three bytes and leaves the count to the pass that built the packet.
//
// So the field is a main-RAM OFFSET, not an address: a packet at 0x80041234 chains as 0x041234.
// Reading it as an address is not a decoding subtlety, it is a wrong number — the offset is a
// million bytes below the arena, so the first multi-packet chain would refuse itself out of the
// arena. `guestAddressOf` is the guest's own round trip (`OrderingTableCursor::nextGuestAddress`
// uses it), not a repair.
inline constexpr std::uint32_t kNextMask = 0x00FFFFFFu;

// THE LONGEST PACKET THE GP0 CAN DESCRIBE IS THE FIFO'S OWN LENGTH. `gpu_gp0` collects a packet's
// words in a 256-word FIFO before executing it, so a packet longer than that cannot be executed at
// all — the bound is the hardware path's, not a preference, and it is what a tag claiming more is
// refused for.
inline constexpr std::uint32_t kMaxPacketWords = 256;

inline std::uint32_t binHead(guest_terrain::TerrainMemory &memory,
                             std::uint32_t table,
                             std::uint32_t bin,
                             const std::vector<std::uint8_t> &linked) {
  if (bin >= linked.size() || linked[bin] == 0) {
    return 0;
  }
  return memory.r32(table + bin * kBinStride + kHeadOffset);
}

// One refusal for a walk it cannot make sense of, and it is a refusal rather than a clamp because a
// silent wrong answer here is a wrong PICTURE, which is the one failure this whole path exists to
// avoid making.
inline void requireBound(const Bound &bound) {
  if (bound.valid()) {
    return;
  }
  lucent::error(
      "terrsink",
      "refusing to walk an in-between field's terrain: the ordering table is at 0x{:08X}, the "
      "packet arena is [0x{:08X},+0x{:X}), and {}",
      bound.tableBase,
      bound.arenaBase,
      bound.arenaBytes,
      bound.linkedBins == nullptr ? "the traversal's linked-bin record is missing" : "nothing is");
  std::abort();
}

} // namespace detail

// Walks every packet in `bound`'s table, in retail's flatten order, and calls `visit` with the
// packet's guest-shaped address and its GP0 words.
//
// THIS IS THE WALK. `submit` is this with a visitor that hands each packet to the framework, and
// the order test is this with a visitor that records the addresses: one implementation, so the
// order the test proves is the order that draws.
template <typename Visit>
void forEachPacket(guest_terrain::TerrainMemory &memory, const Bound &bound, Visit visit) {
  detail::requireBound(bound);

  // Retail starts at the table's own end because its caller asked for bin 1408, which is past the
  // last bin, and walks down; the first non-empty bin found below that is the deepest bin the
  // traversal used, and it is submitted FIRST. The linked-bin record is sized to that bin, so the
  // walk starts there instead of descending 512 empty bins to find it.
  //
  // The packet budget is the arena's own word count plus one: a packet is at least one word, so
  // that is the largest number the arena can hold, and it is also the bound on a cyclic chain,
  // which is the only way this walk could otherwise not terminate.
  const std::uint32_t budget = bound.arenaBytes / 4u + 1u;
  std::uint32_t submitted = 0;
  std::uint32_t words[detail::kMaxPacketWords];

  for (std::uint32_t bin = bound.linkedBins->size(); bin-- > 0;) {
    std::uint32_t packet = detail::binHead(memory, bound.tableBase, bin, *bound.linkedBins);
    while (packet != 0) {
      if (submitted >= budget) {
        lucent::error(
            "terrsink",
            "refusing to walk more than {} packets out of one in-between field's terrain: the "
            "chain from bin {} in a {}-byte arena is cyclic, or the traversal built more than "
            "its own arena can hold",
            budget,
            bin,
            bound.arenaBytes);
        std::abort();
      }
      // The bin's own head is read straight from the table, where the drawer wrote a full address.
      packet &= psx::gpu::kKseg0Base | psx::gpu::kMainRamOffsetMask;
      if (packet < bound.arenaBase || packet >= bound.arenaBase + bound.arenaBytes) {
        lucent::error("terrsink",
                      "refusing to follow a chain link out of the packet arena: bin {} links to "
                      "0x{:08X}, which is outside [0x{:08X},0x{:08X})",
                      bin,
                      packet,
                      bound.arenaBase,
                      bound.arenaBase + bound.arenaBytes);
        std::abort();
      }
      const std::uint32_t tag = memory.r32(packet);
      const std::uint32_t count = tag >> 24;
      if (count == 0 || count > detail::kMaxPacketWords ||
          packet + 4u * count > bound.arenaBase + bound.arenaBytes) {
        lucent::error(
            "terrsink",
            "refusing a {}-word packet at 0x{:08X} in a {}-byte arena: the tag's own word "
            "count is {}",
            count,
            packet,
            bound.arenaBytes,
            count);
        std::abort();
      }
      for (std::uint32_t i = 0; i < count; ++i) {
        words[i] = memory.r32(packet + 4u * i);
      }
      visit(packet, words, count);
      ++submitted;
      // A ZERO next field ends the chain, and it is the only terminator there is — so it is tested
      // as the offset the guest wrote, BEFORE the round trip. `guestAddressOf(0)` is the base of
      // main RAM, not the end of a chain, and testing the reconstructed address would run off the
      // last packet of every bin into 0x80000000.
      const std::uint32_t next = tag & detail::kNextMask;
      packet = next == 0 ? 0 : psx::gpu::guestAddressOf(next);
    }
  }
}

// Every packet in `bound`'s table, through the framework's one guest-packet funnel.
void submit(Core &core, guest_terrain::TerrainMemory &memory, const Bound &bound);

} // namespace spyro::terrain_packet_sink
