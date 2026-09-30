#include "native_glow_sparkle_pools.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// ── 0x8005882C — the index of the first free sparkle slot, or -1 when all eight are still alive.
//     `g_Sparkles` holds eight 0x18-byte records whose byte at +0x0C is the remaining life, so a
//     free slot is one whose life byte reads zero. The scan stops when `$at` reaches the base plus
//     0xC0, which it only ever reaches by stepping PAST the last record, so all eight life bytes
//     are read and none beyond. v0 carries the slot index, or -1 once every record is spoken for,
//     and v1 the life byte the scan stopped on — the zero it found, or the last live record's byte.
constexpr std::uint32_t kSparkles = 0x80077108u;
constexpr std::uint32_t kSparkleStride = 0x18u;
constexpr std::uint32_t kSparkleLifeByte = 0x0Cu;
constexpr std::uint32_t kSparkleSlots = 8u;
constexpr std::uint32_t kNoFreeSparkle = 0xFFFFFFFFu;

void findFreeSparkleSlot(Core *c) {
  std::uint32_t life = 0;
  std::uint32_t slot = 0;
  for (; slot < kSparkleSlots; ++slot) {
    life = c->mem_r8(kSparkles + slot * kSparkleStride + kSparkleLifeByte);
    if (life == 0u) {
      break;
    }
  }
  c->r[3] = life;
  c->r[2] = slot < kSparkleSlots ? slot : kNoFreeSparkle;
}

// ── 0x80058B68 — clear both glow and sparkle pools: the point count at +0x00 of each of the
//     sixteen 0x24-byte glow records (zero is how a glow is switched off), then the life byte at
//     +0x0C of each of the eight 0x18-byte sparkle records. Each loop counts a byte offset down
//     from the LAST record, one stride at a time, while it is still non-negative, so the first
//     store is the topmost glow's count and the second loop's first store is the last sparkle's
//     life byte. v0 exits holding the second counter one stride below zero; v1 is never written.
constexpr std::uint32_t kGlows = 0x80078800u;
constexpr std::uint32_t kGlowStride = 0x24u;
constexpr std::uint32_t kGlowRecords = 16u;

void clearGlowsAndSparkles(Core *c) {
  for (std::uint32_t record = 0; record < kGlowRecords; ++record) {
    c->mem_w32(kGlows + record * kGlowStride, 0);
  }
  for (std::uint32_t slot = 0; slot < kSparkleSlots; ++slot) {
    c->mem_w8(kSparkles + slot * kSparkleStride + kSparkleLifeByte, 0);
  }
  c->r[2] = 0u - kSparkleStride;
}

} // namespace

void registerGlowSparklePoolOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x8005882Cu, "find_free_sparkle_slot", findFreeSparkleSlot);
  spyro::installNativeOverride(
      core, 0x80058B68u, "clear_glows_and_sparkles", clearGlowsAndSparkles);
}

} // namespace spyro1::native
