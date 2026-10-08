// native_collision_shade.cpp — the packed surface-shade word the collision raycast leaves for its
// caller. One magnitude and two quadrant quantisers; the GTE command and the magnitude table are
// the framework's, so this owns the arithmetic around them and not the hardware inside them.
//
// The word it writes is the CALLER's latch, and the top bit it sets is the only thing that stops
// the next call recomputing it — which is why the body reads the word back, keeps it, and rewrites
// it unchanged when it is already positive.
#include "native_collision_shade.h"

#include "core.h"
#include "guest_globals.h"
#include "guest_magnitude.h"
#include "native_execution.h"

#include <cstdint>

namespace spyro1::native {
namespace {

using spyro::guest::kMagnitudeTable;

// g_CollisionNormal (game.bss.s:87) — the three words the raycast leaves the surface normal in,
// read here as x/y/z. g_CollisionPoint's third word is the only part of the hit point the body
// reads, and it becomes the low channel of the packed word UNMASKED, so it is not narrowed to six
// bits with the other two.
constexpr std::uint32_t kCollisionNormal = 0x80077368u;
constexpr std::uint32_t kCollisionHitZ = 0x80076B88u;
constexpr std::uint32_t kShadeWordOffset = 0x1Cu;
constexpr std::uint32_t kPackedShadeFlag = 0x80000000u;
constexpr std::uint32_t kChannelBits = 0x3Fu;
constexpr std::uint32_t kElevationShift = 16u;
constexpr std::uint32_t kAzimuthShift = 22u;

// One of the body's two four-armed quantisers, each reached with a different pair of normal
// components: the first divides the normal's y by its horizontal length, the second divides x by z.
//
// Which arm runs is decided by the SIGNS of `reference - component` and `reference + component`,
// and BOTH of those sums are delay slots — the second is computed while the first branch is already
// resolved and cannot be hoisted past it. Two arms share a dividend and a divisor and differ only
// in the quadrant base they add, so folding the four into a formula would be a guess about which
// two. `a2` keeps the dividend and `a3` the base on every arm: the two arms that negate the divisor
// overwrite `a3` with the base in their `j`'s delay slot.
//
// It returns `mflo` plus that base, which is the value both callers keep. `cpu_div` is the guest's
// own signed divide, because the arms reach it with a divisor of zero and with a negated INT_MIN
// and C++ division is undefined in exactly those two cases.
std::uint32_t quadrantSum(Core *c, std::uint32_t reference, std::uint32_t component) {
  const std::uint32_t difference = reference - component;
  const std::uint32_t total = reference + component;
  std::uint32_t dividend;
  std::uint32_t divisor;
  std::uint32_t base;
  if (static_cast<std::int32_t>(difference) >= 0) {
    if (static_cast<std::int32_t>(total) >= 0) {
      dividend = component << 5;
      divisor = reference;
      base = 0u;
    } else {
      dividend = reference << 5;
      divisor = 0u - component;
      base = 0xC0u;
    }
  } else {
    if (static_cast<std::int32_t>(total) >= 0) {
      dividend = reference << 5;
      divisor = 0u - component;
      base = 0x40u;
    } else {
      dividend = component << 5;
      divisor = reference;
      base = 0x80u;
    }
  }
  c->r[6] = dividend;
  c->r[7] = base;
  cpu_div(c, dividend, divisor);
  return c->lo + base;
}

// ── 0x800533D0 — pack the raycast's surface orientation into the caller's shade word at a0+0x1C.
//     lw at/v0/v1,0..8(0x80077368)     the normal, left in v0, v1 and at
//     mtc2 at->IR1 ; mtc2 v1->IR3 ; GTE 0x4AA00428 (SQR)
//     lw a3,0x1C(a0) ; mfc2 a1<-MAC1 ; mfc2 a2<-MAC3 ; bgez a3,STORE ; add a1,a1,a2
//     the shared magnitude tail on a1, two quadrant quantisers, then the packing below
//     jr ra ; sw a3,0x1C(a0)                            the store is the delay slot: unconditional
//
// THE `add` IS THE FIRST BRANCH'S DELAY SLOT and runs on BOTH arms, so the sum of the two squares
// exists in `a1` even on the arm that skips everything and stores the word back untouched. The arm
// that skips also leaves LZCS alone, which is why the leading-zero count is written after the
// branch rather than with the GTE setup above it.
//
// The GTE holds ONE register this body does not write: mtc2 to $t1 and $t3 sets IR1 and IR3 and
// leaves IR2 as the previous body left it, and SQR's MAC2 is then a square of that. The body reads
// only MAC1 and MAC3, so the horizontal length is x² + z² and never includes y — the quantiser
// compares that length against y afterwards, which is how the normal's elevation is measured.
// Driving the command through the framework's own GTE keeps the discarded MAC2 and the flags
// matching too.
//
// The two quantisers swap which component is the divisor: elevation divides y by the length,
// azimuth divides x by z. Their quotients are the two channels, negated and taken to 63, and their
// order matters because the SECOND divide is the one whose quotient and remainder the body leaves
// behind.
//
// MEASURED, and the coverage is narrower than the call count suggests. On artisans-walk the
// differential samples 1042 of 1042 calls and every one matches — but the word this body writes is
// a LATCH, so 106 of those calls find it already positive, take the early exit and republish it
// unchanged, and only 936 reach the arithmetic. An instrumented build counting arms reported
// 936/0/0/0 for the elevation quantiser and 936/0/0/0 for the azimuth one, and portal-level added
// 1978 more, all arm 1. So the other three arms of each quantiser, and the zero-magnitude tail, are
// unexercised and follow the retail disassembly alone. That is a property of the corpus rather than
// of the run length: arm 1 is what a normal at least as horizontal as vertical produces, and no
// route here hits a wall or a ceiling. The instrument does report this class of difference —
// poisoning the 0x3F channel mask to 0x3E is caught on 455 of those 1042 calls, first at call 8 —
// so the unexercised arms are unchecked rather than quietly different.
void packCollisionShadeWord(Core *c) {
  const std::uint32_t normalX = c->mem_r32(kCollisionNormal + 0u);
  const std::uint32_t normalY = c->mem_r32(kCollisionNormal + 4u);
  const std::uint32_t normalZ = c->mem_r32(kCollisionNormal + 8u);
  c->r[1] = normalX; // at
  c->r[2] = normalY; // v0
  c->r[3] = normalZ; // v1

  gte_write_data(9, normalX);  // IR1
  gte_write_data(11, normalZ); // IR3
  gte_op(c, 0x4AA00428u);      // SQR(sf=0, lm=1)

  const std::uint32_t caller = c->r[4];
  const std::uint32_t stored = c->mem_r32(caller + kShadeWordOffset);
  c->r[7] = stored;                                 // a3
  const std::uint32_t xSquared = gte_read_data(25); // MAC1
  const std::uint32_t zSquared = gte_read_data(27); // MAC3
  c->r[5] = xSquared;                               // a1
  c->r[6] = zSquared;                               // a2
  const std::uint32_t horizontal = xSquared + zSquared;
  c->r[5] = horizontal; // a1 — the `add` runs on both arms
  if (static_cast<std::int32_t>(stored) >= 0) {
    c->mem_w32(caller + kShadeWordOffset, stored);
    return;
  }

  gte_write_data(30, horizontal); // LZCS — the count happens on the write
  std::uint32_t magnitude = 0u;
  if (horizontal != 0u) {
    const auto step = spyro::guest_magnitude::normalize(horizontal, gte_read_data(31));
    c->r[8] = step.tableByteOffset; // t0
    magnitude = spyro::guest_magnitude::scaled(
                    static_cast<std::int16_t>(c->mem_r16(kMagnitudeTable + step.tableByteOffset)),
                    step.exponent) >>
                12;
  }
  c->r[5] = magnitude;                          // a1
  c->r[2] = quadrantSum(c, magnitude, normalY); // v0 — the elevation channel
  c->r[1] = quadrantSum(c, normalZ, normalX);   // at  — the azimuth channel

  const std::uint32_t hitZ = c->mem_r32(kCollisionHitZ);
  c->r[5] = hitZ; // a1
  c->r[1] = 0u - c->r[1];
  c->r[2] = 0u - c->r[2];
  c->r[1] >>= 2; // srl, not sra
  c->r[2] >>= 2;
  c->r[1] &= kChannelBits;
  c->r[2] &= kChannelBits;
  c->r[1] <<= kElevationShift;
  c->r[2] <<= kAzimuthShift;
  std::uint32_t packed = kPackedShadeFlag;
  packed |= c->r[1];
  c->r[3] = packed; // v1
  packed |= c->r[2];
  c->r[3] = packed;
  const std::uint32_t lowChannel = hitZ >> 2;
  c->r[5] = lowChannel; // a1
  const std::uint32_t word = packed | lowChannel;
  c->r[7] = word; // a3
  c->mem_w32(caller + kShadeWordOffset, word);
}

} // namespace

void registerCollisionShadeOverrides(Core &core) {
  psx::cpu::installNativeOverride(
      core, 0x800533D0u, "collision_shade_word", packCollisionShadeWord);
}

} // namespace spyro1::native
