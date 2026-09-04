// native_rand.cpp — the first guest function this port OWNS rather than observes.
//
// WHY THIS ONE FIRST. It is a real native replacement, chosen to be small and exactly specified so
// native-override registration can be proven before it is pointed at anything load-bearing.
//
// THE FUNCTION. 0x8006272C is Sony's rand(): the standard LCG, verified from its own disassembly
// rather than assumed from the constants.
//
//   8006272C  lui  v1, 0x41C6
//   80062730  lw   v0, [0x80075AC0]      ; seed
//   80062738  ori  v1, v1, 0x4E6D        ; v1 = 0x41C64E6D
//   8006273C  mult v0, v1
//   80062740  mflo a0
//   80062744  addiu v0, a0, 12345
//   8006274C  sw   v0, [0x80075AC0]      ; seed = seed*0x41C64E6D + 12345
//   80062750  srl  v0, v0, 16
//   80062758  andi v0, v0, 0x7FFF        ; return (seed >> 16) & 0x7FFF
//
// It matters that this is exact rather than merely random: the title screen branches on rand()&3 to
// pick its idle animation (C071), so a different sequence changes observable behaviour and would
// make any future frame-for-frame comparison drift for reasons that have nothing to do with the
// code under test.
//
// HI/LO ARE PART OF THE CONTRACT, and this is the subtlety worth stating. `mult` writes the hi/lo
// register pair, and the runtime models them. A native body that computes the right RETURN VALUE
// but leaves hi/lo stale is NOT equivalent — any guest code that reads hi/lo before the next mult
// would diverge. That is exactly the kind of difference a human reviewer waves past and a per-call
// differential catches, so the native body sets them explicitly.
#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kSeed = 0x80075AC0u;
constexpr uint32_t kMul = 0x41C64E6Du;

void rand_native(Core *c) {
  const uint32_t seed = c->mem_r32(kSeed);
  // The 64-bit product is what `mult` leaves in hi/lo; the low half is what the LCG uses. Compute
  // it signed, because MIPS `mult` is a SIGNED multiply and the runtime models it that way —
  // using an unsigned product would give the same low word but the wrong `hi`.
  const int64_t prod = (int64_t)(int32_t)seed * (int64_t)(int32_t)kMul;
  const uint32_t lo = (uint32_t)(prod & 0xFFFFFFFFu);
  const uint32_t hi = (uint32_t)((uint64_t)prod >> 32);
  const uint32_t next = lo + 12345u;

  c->mem_w32(kSeed, next);
  c->lo = lo;
  c->hi = hi;
  c->r[4] = lo;                     // a0 = mflo, left live exactly as the body leaves it
  c->r[3] = kMul;                   // v1 = the multiplier the body built with lui/ori
  c->r[2] = (next >> 16) & 0x7FFFu; // v0 = return value
  // $at IS PART OF THE OBSERVABLE RESULT, however much it should not be. The body's last `lui at,
  // 0x8007` (0x80062748, building the seed address for the store) leaves 0x80070000 behind, and the
  // per-call differential flagged the mismatch on call #10 — the first call where the caller
  // happened to leave a different value in $at. Architecturally $at is the assembler temporary and
  // no host code reads it across a call, so this is harmless in practice. It is
  // reproduced anyway because "harmless" is a judgement and "identical" is a measurement, and the
  // moment a replacement is allowed to differ "where it does not matter" the differential stops
  // meaning anything. I would not have noticed this by reading the code.
  c->r[1] = 0x80070000u;
}

} // namespace

void spyro_register_native_rand(Core &core) {
  spyro::installNativeOverride(core, 0x8006272Cu, "rand", rand_native);
}
