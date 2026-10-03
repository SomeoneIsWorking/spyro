// native_rand.cpp — the native replacement for Sony's rand() at 0x8006272C, verified from its own
// disassembly rather than assumed from the constants:
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
// The sequence is exact rather than merely random: the title screen branches on rand()&3 to pick
// its idle animation, so a different sequence changes observable behaviour.
//
// HI/LO ARE PART OF THE CONTRACT: `mult` writes the register pair and the runtime models it, so a
// body that computes the right return value but leaves hi/lo stale is not equivalent. `$at` is
// reproduced for the same reason — "harmless" is a judgement and "identical" is a measurement.
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
  // The body's last `lui at, 0x8007` (0x80062748, building the seed address for the store) leaves
  // 0x80070000 behind, and the per-call differential flagged the mismatch.
  c->r[1] = 0x80070000u;
}

} // namespace

void spyro::registerNativeRand(Core &core) {
  // The literal, not kRandEntry: tools/reach_corpus.py and tools/override_constants.py both read
  // the registered entry address out of this call, so the owner's own registration spells it.
  spyro::installNativeOverride(core, 0x8006272Cu, "rand", rand_native);
}
