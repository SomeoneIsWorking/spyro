// The native replacement for Sony's rand() at 0x8006272C, from its own disassembly:
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
// The sequence is exact: the title screen branches on rand()&3 to pick its idle animation.
//
// hi/lo are part of the contract — `mult` writes the register pair and the runtime models it, so a
// body that computes the right return value but leaves hi/lo stale is not equivalent.
#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

constexpr uint32_t kSeed = 0x80075AC0u;
constexpr uint32_t kMul = 0x41C64E6Du;

void rand_native(Core *c) {
  const uint32_t seed = c->mem_r32(kSeed);
  // The 64-bit product is what `mult` leaves in hi/lo; it is signed because MIPS `mult` is, and an
  // unsigned product would give the same low word but the wrong `hi`.
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
  // The body's last `lui at, 0x8007` (0x80062748) builds the seed address and leaves this behind.
  c->r[1] = 0x80070000u;
}

} // namespace

void spyro::registerNativeRand(Core &core) {
  // A literal rather than a named constant: an address audit reads the entry address out of this
  // call.
  psx::cpu::installNativeOverride(core, 0x8006272Cu, "rand", rand_native);
}
