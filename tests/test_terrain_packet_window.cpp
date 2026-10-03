// test_terrain_packet_window.cpp — the packet arena an in-between field gets, and the window it
// must not overlap.
//
// The rule under test (terrain_world_pass.h, `packetArenaWindow`) exists because of a measured
// fault, not because of a preference: the guest allocates its terrain packets out of a pool that
// grows upward from its own cursor, and by the time the presenter rebuilds an in-between frame that
// cursor has already advanced past everything the field's own producers allocated. MEASURED
// 2026-10-03 on SCUS_944.25, an in-between starting at 0x801D04B4 needed 35,468 bytes of packets
// while the scratch block of the frame it was drawing began at 0x801D7444 — 6,844 bytes short — and
// the packets it wrote over that scratch block destroyed the sector and split lists, so
// walkSplitList read the split-list word 0x0C000000 as a sector header and faulted reading
// 0x30000008. SCUS_944.67 aborted on the same line, which is the same bug on a title that reached
// it first.
//
// The three properties asserted here are the ones that bug violated, and each is a boundary:
//   the window starts ABOVE every range the traversal owns, so a packet can never land on one;
//   it is page-aligned and runs to the end of the 2 MB a guest packet address can name, because a
//   chain link is a 24-bit main-RAM OFFSET and a shorter or unaligned window would refuse writes
//   the traversal is entitled to make; and a `frameTop` at or above that end is the REFUSAL case
//   (bytes == 0), because a clamped arena is a silently truncated picture.
#include "terrain_world_pass.h"

#include "testutil.h"

namespace {

// The 2 MB main-RAM window, and the address the level's own records live at (MEASURED: sector and
// polygon records at 0x8008xxxx, scratch blocks ending at 0x801DA844). Neither is a constant this
// header needs; they are here so the asserted windows are the ones the product meets.
constexpr std::uint32_t kMainRamEnd = 0x80200000u;
constexpr std::uint32_t kMainRamBase = 0x80000000u;

} // namespace

static void test_window_starts_above_everything_the_frame_owns() {
  // SCUS_944.25's own measured layout: the fogged-colour buffer ends first, the ordering table's
  // window next, the scratch block last.
  const std::uint32_t foggedColours = 0x800683F0u + 0x1000u;
  const std::uint32_t orderingWindow = 0x8006C000u + 0x2000u;
  const std::uint32_t scratchBlock = 0x801D7444u + 0x3400u;
  const std::uint32_t frameTop = std::max({foggedColours, orderingWindow, scratchBlock});

  const spyro::PacketArena arena = spyro::packetArenaWindow(frameTop, kMainRamEnd);
  CHECK(arena.bytes != 0);
  // Above every range, and page-aligned, so no packet can land on one of them.
  CHECK(arena.base > frameTop);
  CHECK_EQ(arena.base & 0xFFFu, 0u);
  CHECK(arena.base >= foggedColours);
  CHECK(arena.base >= orderingWindow);
  CHECK(arena.base >= scratchBlock);
  // And it runs to the end of the window a guest packet address can name.
  CHECK_EQ(arena.base + arena.bytes, kMainRamEnd);
  // Every address in it is a guest-shaped main-RAM one, so a chain link's 24-bit offset can name
  // it.
  CHECK(arena.base >= kMainRamBase);
  CHECK(arena.base + arena.bytes <= kMainRamEnd);
}

static void test_window_is_large_enough_for_a_field() {
  // The refusal bound the caller applies (kMinArenaBytes, 64 KB) against the widest arena the
  // measured layout leaves: MEASURED 2026-10-03, the scratch block of the frame in use ends at
  // 0x801DA844, so the window above it is the smallest this product ever hands out.
  const spyro::PacketArena arena = spyro::packetArenaWindow(0x801DA844u, kMainRamEnd);
  CHECK(arena.bytes != 0);
  CHECK(arena.bytes >= 0x10000u);
}

static void test_no_window_is_a_refusal_not_a_clamp() {
  // `frameTop` already at the end of main RAM: there is no arena, and the answer says so with zero
  // bytes rather than with a window that would silently drop the tail of the frame's packets.
  CHECK_EQ(spyro::packetArenaWindow(kMainRamEnd, kMainRamEnd).bytes, 0u);
  // Above it, which is what an arithmetic subtraction would have turned into a huge positive span.
  CHECK_EQ(spyro::packetArenaWindow(kMainRamEnd + 0x1000u, kMainRamEnd).bytes, 0u);
  // One byte below the end: a window of one byte, which is still a refusal at the caller's bound
  // and is reported as the window it is rather than as nothing at all.
  const spyro::PacketArena sliver = spyro::packetArenaWindow(kMainRamEnd - 0x1000u, kMainRamEnd);
  CHECK_EQ(sliver.base, kMainRamEnd - 0x1000u);
  CHECK_EQ(sliver.bytes, 0x1000u);
}

int main() {
  RUN(window_starts_above_everything_the_frame_owns);
  RUN(window_is_large_enough_for_a_field);
  RUN(no_window_is_a_refusal_not_a_clamp);
  return pt_summary();
}
