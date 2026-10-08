#include "native_spu_key_status.h"

#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

// The two globals the body reads, each named by the retail `lui 0x8007` plus a POSITIVE 16-bit
// displacement the override constant check re-derives: the base pointer of the 24-entry key table,
// and the word of keys the SPU currently holds down.
constexpr std::uint32_t kSpuKeyTable = 0x80073554u;
constexpr std::uint32_t kSpuKeyDownFlags = 0x800730ECu;
constexpr std::uint32_t kKeySlotStride = 0x10u;
constexpr std::uint32_t kKeySlotOnHalfword = 0x0Cu;
constexpr std::uint32_t kKeySlotCount = 24u;
constexpr std::uint32_t kNoSuchKey = 0xFFFFFFFFu;

// ── 0x8005C9D0 — the status of one SPU key. The argument is a MASK, not an index: the body scans
//     it for its lowest set bit below 24 and answers -1 when it has none.
// The `sll a0, a1, 4` that turns that bit index into a table offset sits in the DELAY SLOT of the
// `bne a1, -1` below it, so it runs on BOTH arms — and it is what makes the table entry the bit
// index names rather than anything the caller passed, so a mask with several bits set reads the
// lowest one and ignores the rest.
// v0 is one of four codes, from the down flag and the entry's own halfword: 1 when the key is down
// and that halfword is set, 3 when it is down and the halfword is clear, 2 when it is up and the
// halfword is set, 0 when both are clear. v1 carries the down flag itself (0 or 1, a single bit
// masked out of the global) on every path that found a key, and on the -1 exit the scan counter
// the loop gave up at, 24. The body performs no guest store at all.
//
// MEASURED on route artisans-walk: 1232 calls seen, 1232 sampled, 1232 match. An instrumented
// build of this same body, over the same 1232 calls, counted the exits the route reaches — v0=1
// 1221 times, v0=3 ten times, v0=0 once, with arguments that are only ever a single key bit
// (0x1..0x40, keys 0..6). So the -1 exit and the v0=2 code are NOT exercised by the route and
// follow the retail disassembly alone: every call the route makes names a live key, and no key is
// ever seen both up and set. Those are the -1 arm's own three instructions (0x8005C9FC, the `j`
// and its `nop`) plus the one unexercised outcome of the `sltu`/`sll` pair the v0=0 call did
// reach.
void spuKeyStatus(Core *c) {
  const std::uint32_t requested = c->r[4];
  std::uint32_t key = kNoSuchKey;
  std::uint32_t scanned = 0u;
  for (scanned = 0u; scanned < kKeySlotCount; ++scanned) {
    if ((requested & (1u << scanned)) != 0u) {
      key = scanned;
      break;
    }
  }
  if (key == kNoSuchKey) {
    c->r[2] = kNoSuchKey;
    c->r[3] = scanned;
    return;
  }
  const std::uint32_t entry = c->mem_r32(kSpuKeyTable) + key * kKeySlotStride;
  const std::uint32_t down = c->mem_r32(kSpuKeyDownFlags) & (1u << key);
  const std::uint32_t on = c->mem_r16(entry + kKeySlotOnHalfword);
  c->r[3] = down;
  c->r[2] = down != 0u ? (on != 0u ? 1u : 3u) : (on != 0u ? 2u : 0u);
}

} // namespace

void registerSpuKeyStatusOverrides(Core &core) {
  psx::cpu::installNativeOverride(core, 0x8005C9D0u, "spu_key_status", spuKeyStatus);
}

} // namespace spyro1::native
