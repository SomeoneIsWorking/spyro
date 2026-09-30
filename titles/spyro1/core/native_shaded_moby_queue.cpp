#include "native_shaded_moby_queue.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

constexpr std::uint32_t kShadedMobyList = 0x800720F4u;  // g_SonyImage.m_ShadedMobys
constexpr std::uint32_t kHudMobyCursor = 0x80075710u;   // g_HudMobys
constexpr std::uint32_t kHudMobyArenaEnd = 0x800756FCu; // D_800756FC
constexpr std::uint32_t kQueueSlotBytes = 4u;
constexpr std::uint32_t kMobyBytes = 0x58u; // the stride g_HudMobys is walked by
constexpr std::uint32_t kGuestSegmentBase =
    0x80070000u; // the `lui $at,0x8007` the cursor store forms

// ── 0x80018880 — append the mobys built into the HUD arena since the last frame to the shaded-Moby
//     list: find that list's first empty slot, then copy the moby pointers g_HudMobys has advanced
//     past into it, one 0x58-stride Moby at a time, up to the arena end D_800756FC.
// Three delay slots decide the exit state. The zero that terminates the list is the `jr $ra` delay
// slot, so it is written on BOTH exits, including the already-drained one that runs no loop at all;
// the queue advance is the `bne` delay slot, so it runs on the last pass too and the terminator
// lands one slot PAST the last moby copied; and the empty-slot scan's own `addiu $a0,$a0,-4` undoes
// the `+4` that scan's branch delay slot added, leaving a0 on the empty slot rather than past it.
// v0 and v1 both exit holding the arena-end word — the loop ends on the one pass that reaches it —
// and $at holds the bare `lui $at,0x8007` result rather than an address, but only on the path that
// stores the cursor: a drained arena leaves it as the caller had it.
void appendHudMobysToShadedQueue(Core *c) {
  std::uint32_t slot = kShadedMobyList;
  while (c->mem_r32(slot) != 0u) {
    slot += kQueueSlotBytes;
  }
  const std::uint32_t arenaEnd = c->mem_r32(kHudMobyArenaEnd);
  std::uint32_t cursor = c->mem_r32(kHudMobyCursor);
  if (cursor != arenaEnd) {
    c->r[1] = kGuestSegmentBase;
    do {
      c->mem_w32(slot, cursor);
      cursor += kMobyBytes;
      c->mem_w32(kHudMobyCursor, cursor);
      slot += kQueueSlotBytes;
    } while (cursor != arenaEnd);
  }
  c->mem_w32(slot, 0u);
  c->r[2] = arenaEnd;
  c->r[3] = arenaEnd;
  c->r[4] = slot;
}

} // namespace

void registerShadedMobyQueueOverrides(Core &core) {
  spyro::installNativeOverride(
      core, 0x80018880u, "append_hud_mobys_to_shaded_queue", appendHudMobysToShadedQueue);
}

} // namespace spyro1::native
