#include "native_particle_alloc.h"

#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {

namespace {

constexpr std::uint32_t kParticlePool = 0x80075824u;
constexpr std::uint32_t kPoolCursor = 0x80075738u;
constexpr std::uint32_t kSlotStride = 0x20u;
constexpr std::uint32_t kPoolSpan = 0x2000u;
constexpr std::uint32_t kSlotType = 0x1u;
constexpr std::uint32_t kFreeSlotByte = 0xFFu;

constexpr std::uint32_t kGroupSlots = 4u;
constexpr std::uint32_t kGroupSlotStride = 0x18u;
constexpr std::uint32_t kGroupFirstSlot = 0x44u;
constexpr std::uint32_t kUnsetExtent = 0x7F7F7F7Fu;
// Sony's rand(), the standard LCG game/core/native_rand.cpp owns, reached by the `jal` at
// 0x80053598 in the full-allocation arm.
constexpr std::uint32_t kSonyRand = 0x8006272Cu;

// 0x80053570 — stamp a0's low byte as the type of the next free slot in the 256-slot particle pool
// and leave g_ParticleAllocPtr on it. Two things are not what the shape suggests. The scan's
// `addiu v1,v1,0x20` is the bgez delay slot and the `addiu v1,v1,-0x20` behind it undoes that on
// the exit arm, so an untaken advance leaves the cursor standing. And the full arm calls rand()
// BEFORE the `andi v0,v0,0xff`, so the wrap is driven by rand's return rather than by the cursor
// the `lw` left in v0 — and it never writes g_ParticleAllocPtr back at all. v0 exits as the stamped
// slot on that arm and as the loaded cursor on every other; v1 as 0x20 there and as the new cursor
// everywhere else. at exits as g_ParticleAllocPtr, and as the bare lui high half once rand() ran.
void allocateParticleSlot(Core *c) {
  const std::uint32_t kind = c->r[4];
  const std::uint32_t limit = c->mem_r32(kParticlePool) + kPoolSpan;
  const std::uint32_t cursor = c->mem_r32(kPoolCursor);
  c->r[2] = cursor;
  c->r[1] = kPoolCursor;
  if (cursor == limit) {
    psx::cpu::callGuestNow(*c, "allocate_particle_slot", kSonyRand, kind, limit, kind);
    const std::uint32_t span = (c->r[2] & 0xFFu) + 1u;
    const std::uint32_t slot = limit - span * kSlotStride;
    c->lo = span * kSlotStride;
    c->hi = 0u;
    c->mem_w8(slot + kSlotType, static_cast<std::uint8_t>(kind));
    c->r[2] = slot;
    c->r[3] = kSlotStride;
    return;
  }
  const std::int32_t previous = c->mem_r8s(cursor + kSlotType);
  c->mem_w8(cursor + kSlotType, static_cast<std::uint8_t>(kind));
  if (previous != -1) {
    std::uint32_t scan = cursor + kSlotStride;
    while (scan != limit && c->mem_r8s(scan + kSlotType) >= 0) {
      scan += kSlotStride;
    }
    c->r[3] = scan;
    c->mem_w32(kPoolCursor, scan);
    return;
  }
  const std::uint32_t next = cursor + kSlotStride;
  if (next != limit) {
    c->mem_w8(next + kSlotType, kFreeSlotByte);
  }
  c->r[3] = next;
  c->mem_w32(kPoolCursor, next);
}

// 0x800536A4 — clear the particle group at a0 and re-arm its four 0x18-byte slots: three state
// words to zero, the +0x14 extent word to 0x7F7F7F7F, both counters to 1, and each slot's +0x14 to
// the same sentinel with a copy of the group's own +0x0C word at slot +0. That +0x0C load is inside
// the loop but the group never writes it, so one read serves all four slots. v0 exits as the final
// slti, which is 0 because the loop stops on the fourth count, and v1 as the walk cursor — the
// group's base plus four strides, which is 0x2C short of where the fourth slot's own stores went.
void resetParticleGroup(Core *c) {
  const std::uint32_t group = c->r[4];
  const std::uint32_t shared = c->mem_r32(group + 0x0Cu);
  c->mem_w32(group + 0x00u, 0u);
  c->mem_w32(group + 0x04u, 0u);
  c->mem_w32(group + 0x08u, 0u);
  c->mem_w32(group + 0x14u, kUnsetExtent);
  c->mem_w32(group + 0x18u, 1u);
  c->mem_w32(group + 0x1Cu, 1u);
  for (std::uint32_t index = 0; index < kGroupSlots; ++index) {
    const std::uint32_t slot = group + kGroupFirstSlot + index * kGroupSlotStride;
    c->mem_w32(slot + 0x00u, shared);
    c->mem_w32(slot + 0x04u, 0u);
    c->mem_w32(slot + 0x08u, 0u);
    c->mem_w32(slot + 0x0Cu, 0u);
    c->mem_w32(slot + 0x14u, kUnsetExtent);
  }
  c->r[2] = 0u;
  c->r[3] = group + kGroupSlots * kGroupSlotStride;
}

} // namespace

void registerParticleAllocOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80053570u, "allocate_particle_slot", allocateParticleSlot);
  spyro::installNativeOverride(core, 0x800536A4u, "reset_particle_group", resetParticleGroup);
}

} // namespace spyro1::native
