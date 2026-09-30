#include "native_random_range.h"

#include "core.h"
#include "guest_call.h"
#include "native_execution.h"

#include <cstdint>
#include <limits>

namespace spyro1::native {

namespace {

// Sony's rand(), the standard LCG game/core/native_rand.cpp replaces with its verified body, and
// the draw this override takes its value from. Its `jal` at 0x80037EB4 is the only call in the
// body, and its own sole `lui` builds the divide-overflow guard's 0x80000000, so the address comes
// from that `jal` and from nowhere else in the code.
constexpr std::uint32_t kSonyRand = 0x8006272Cu;

// ── 0x80037EA0 — one draw from the inclusive range [a0, a1]: a0 + rand() % (a1 - a0 + 1).
//     jal 0x8006272C with a0 and a1 untouched, so the range words are what rand() sees.
//     s0 = a1 - a0 + 1 ; div $zero, v0, s0 ; v0 = mfhi + s1
// The count is `subu`/`addiu`, so it wraps, and the divide is the signed `div` rather than `divu`,
// so what is added to a0 is a signed remainder. $at exits holding 0x80000000: the `lui $at,0x8000`
// in the delay slot of the `bne $s0,$at` that guards the divide. v1 holds the multiplier rand
// itself builds and leaves behind, and hi and lo are this body's `div`, not rand's `mult`. The two
// `break` arms retail guards the divide with (a zero count, then INT_MIN over -1) raise a guest
// exception and return nothing at all, so they leave v0 as the call left it.
void randomInRange(Core *c) {
  const std::uint32_t low = c->r[4];
  const std::uint32_t count = c->r[5] - low + 1u;
  psx::cpu::callGuestNow(*c, "random_in_range", kSonyRand, low, c->r[5]);
  const std::int32_t draw = static_cast<std::int32_t>(c->r[2]);
  if (count == 0u || (count == 0xFFFFFFFFu && draw == std::numeric_limits<std::int32_t>::min())) {
    return;
  }
  const std::int32_t span = static_cast<std::int32_t>(count);
  const std::int32_t remainder = draw % span;
  c->lo = static_cast<std::uint32_t>(draw / span);
  c->hi = static_cast<std::uint32_t>(remainder);
  c->r[1] = 0x80000000u;
  c->r[2] = static_cast<std::uint32_t>(remainder) + low;
}

} // namespace

void registerRandomRangeOverrides(Core &core) {
  spyro::installNativeOverride(core, 0x80037EA0u, "random_in_range", randomInRange);
}

} // namespace spyro1::native
