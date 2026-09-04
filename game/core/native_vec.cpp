// native_vec.cpp — the 3-word vector arithmetic leaves, and libgte's angle-table interpolator.
//
// Same discipline as native_leaf.cpp: leaves only, every register the guest leaves reproduced, with
// recorded differential evidence from real calls.
#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

// ── 0x80017758 — dst = a + b, three words. 102 static callers.
//     lw at/v0/v1 from a1 ; lw a3/t0/t1 from a2 ; add ; sw to a0
// Six registers are left live (at, v0, v1, a3, t0, t1) and all six are reproduced. Note the adds
// are `add`, not `addu` — signed, which on real MIPS traps on overflow. The runtime does not
// trap, so C++ signed addition (which is UB on overflow) is the wrong tool: use unsigned
// arithmetic, which wraps exactly the way the runtime's does.
void vadd_native(Core *c) {
  const uint32_t dst = c->r[4], a = c->r[5], b = c->r[6];
  const uint32_t a0_ = c->mem_r32(a + 0), a1_ = c->mem_r32(a + 4), a2_ = c->mem_r32(a + 8);
  const uint32_t b0 = c->mem_r32(b + 0), b1 = c->mem_r32(b + 4), b2 = c->mem_r32(b + 8);
  const uint32_t s0 = a0_ + b0, s1 = a1_ + b1, s2 = a2_ + b2;
  c->mem_w32(dst + 0, s0);
  c->mem_w32(dst + 4, s1);
  c->mem_w32(dst + 8, s2);
  c->r[1] = s0;
  c->r[2] = s1;
  c->r[3] = s2; // at, v0, v1 — the sums
  c->r[7] = b0;
  c->r[8] = b1;
  c->r[9] = b2; // a3, t0, t1 — b's words, left live
}

// ── 0x8001778C — dst = a - b, three words. 83 static callers. Identical shape to vadd.
void vsub_native(Core *c) {
  const uint32_t dst = c->r[4], a = c->r[5], b = c->r[6];
  const uint32_t a0_ = c->mem_r32(a + 0), a1_ = c->mem_r32(a + 4), a2_ = c->mem_r32(a + 8);
  const uint32_t b0 = c->mem_r32(b + 0), b1 = c->mem_r32(b + 4), b2 = c->mem_r32(b + 8);
  const uint32_t d0 = a0_ - b0, d1 = a1_ - b1, d2 = a2_ - b2;
  c->mem_w32(dst + 0, d0);
  c->mem_w32(dst + 4, d1);
  c->mem_w32(dst + 8, d2);
  c->r[1] = d0;
  c->r[2] = d1;
  c->r[3] = d2;
  c->r[7] = b0;
  c->r[8] = b1;
  c->r[9] = b2;
}

// ── 0x80016CB0 — interpolate a 16-bit table by the low 4 bits of a 12-bit angle. 69 static
// callers.
//
//     andi a0, a0, 0x0FFF        ; wrap to 12 bits
//     at = 0x8006CC78            ; the table (lui 0x8007 ; addiu -13192 = -0x3388)
// I first wrote 0x80073C78 here — a mis-subtraction — and the differential caught it on call #1
// with `a0: native=0x80073C94 guest=0x8006CC94`, a clean 0x7000 apart. This is exactly the
// address the project rule says never to guess; the per-call check turns a guess into a measurement
// in one run.
//     andi v1, a0, 0x000F        ; fractional part
//     beq  v1, zero -> exact entry
//     srl  a0, a0, 4             ; table index
//     sll  a0, a0, 1             ; *2 (16-bit entries)
//     add  a0, a0, at            ; &tbl[i]
//     lh   at, 0(a0) ; lh v0, 2(a0)
//     sub  v0, v0, at            ; delta to the next entry
//     mult v1, v0 ; mflo v0 ; sra v0, v0, 4
//     add  v0, v0, at            ; tbl[i] + frac*delta/16
//
// A piecewise-linear sin/cos-style lookup. Two things must be exact rather than merely equivalent:
// the loads are `lh` (SIGN-extended halfwords), and `mult`/`mflo`/`sra` is a SIGNED multiply whose
// low word is then arithmetically shifted — using unsigned or a plain divide would differ on
// negative deltas, which is most of a signed wave table.
//
// The zero-fraction path is a separate arm (0x80016CF4) with its own register effects, so it is
// read from guest execution rather than guessed; both arms are reproduced below.
void angtbl_body(Core *c, uint32_t kTbl) {
  const uint32_t ang = c->r[4] & 0x0FFFu;
  const uint32_t frac = ang & 0x0Fu;
  if (frac == 0) {
    // The exact-entry arm, READ from 0x80016CF4 rather than assumed — I first wrote `a0 = idx` here
    // and the real body leaves the ADDRESS:
    //     sll a0,a0,1 ; add a0,a0,at ; lh v0,0(a0) ; jr ra
    // It shares the `srl a0,a0,4` in the branch delay slot, so a0 arrives already divided by 16.
    const uint32_t idx = ang >> 4;
    const uint32_t addr = kTbl + idx * 2u;
    c->r[4] = addr;                                // a0 = &tbl[i]
    c->r[1] = kTbl;                                // at — the table base
    c->r[3] = 0;                                   // v1 — the zero fraction
    c->r[2] = (uint32_t)(int16_t)c->mem_r16(addr); // v0 — the entry, sign-extended
    return;
  }
  const uint32_t idx = ang >> 4;
  const uint32_t addr = kTbl + idx * 2u;
  const int32_t lo = (int16_t)c->mem_r16(addr);
  const int32_t hi = (int16_t)c->mem_r16(addr + 2);
  const int32_t delta = hi - lo;
  const int64_t prod = (int64_t)(int32_t)frac * (int64_t)delta;
  const uint32_t plo = (uint32_t)(prod & 0xFFFFFFFFu);
  const uint32_t phi = (uint32_t)((uint64_t)prod >> 32);
  const int32_t scaled = (int32_t)plo >> 4; // sra — arithmetic, not logical
  c->lo = plo;
  c->hi = phi;
  c->r[4] = addr;                    // a0 = &tbl[i] after the shifts and add
  c->r[1] = (uint32_t)lo;            // at = tbl[i]
  c->r[3] = frac;                    // v1 = the fraction
  c->r[2] = (uint32_t)(scaled + lo); // v0 = result
}

// 0x80016CB0 and 0x80016C58 are the SAME body against different tables — 0x8006CC78 and 0x8006CBF8,
// exactly 128 bytes (64 entries) apart, i.e. a quarter turn: the cos/sin pair of one table. Shared
// rather than duplicated, with the base passed in, so a fix to the interpolation cannot be applied
// to one and forgotten in the other.
void angtbl_a_native(Core *c) {
  angtbl_body(c, 0x8006CC78u);
} // lui 0x8007 ; addiu -13192
void angtbl_b_native(Core *c) {
  angtbl_body(c, 0x8006CBF8u);
} // lui 0x8007 ; addiu -13320

// ── 0x80017928 — shortest angular distance between two 12-bit angles. 26 static callers.
//     sub a0,a0,a1 ; andi a0,a0,0x0FFF ; addi a1,a0,-2048
//     bltz a1, +2 ; addi a1,zero,4096   <- delay slot, ALWAYS executed
//     sub a0,a1,a0 ; jr ra ; addi v0,a0,0
// The delay slot is the whole subtlety: `bltz` tests the OLD a1 (= d - 2048), and the slot then
// overwrites a1 with 4096 on BOTH paths. So a1 is 4096 on exit regardless of which arm ran, and the
// fold `d = 4096 - d` happens only for d >= 2048.
void angdist_native(Core *c) {
  const uint32_t d0 = (c->r[4] - c->r[5]) & 0x0FFFu;
  const uint32_t d = (d0 >= 2048u) ? (4096u - d0) : d0;
  c->r[4] = d;
  c->r[5] = 4096u; // a1 — set in the delay slot on both paths
  c->r[2] = d;     // v0
}

// ── 0x800176C8 — arithmetic-shift a 3-word vector IN PLACE by a1. 24 static callers.
//     lw at/v0/v1 from a0 ; srav each by a1 ; sw back to a0
// `srav rd,rt,rs` takes the amount from rs (a1) masked to 5 bits, and it is ARITHMETIC — a logical
// shift would differ on every negative component, which for a signed vector is half the inputs.
void vsra_native(Core *c) {
  const uint32_t p = c->r[4];
  const int sh = (int)(c->r[5] & 31u);
  const int32_t w0 = (int32_t)c->mem_r32(p + 0) >> sh;
  const int32_t w1 = (int32_t)c->mem_r32(p + 4) >> sh;
  const int32_t w2 = (int32_t)c->mem_r32(p + 8) >> sh;
  c->mem_w32(p + 0, (uint32_t)w0);
  c->mem_w32(p + 4, (uint32_t)w1);
  c->mem_w32(p + 8, (uint32_t)w2);
  c->r[1] = (uint32_t)w0;
  c->r[2] = (uint32_t)w1;
  c->r[3] = (uint32_t)w2;
}

} // namespace

void spyro_register_native_vec(Core &core) {
  spyro::installNativeOverride(core, 0x80017758u, "vadd", vadd_native);
  spyro::installNativeOverride(core, 0x8001778Cu, "vsub", vsub_native);
  spyro::installNativeOverride(core, 0x80016CB0u, "angtblA", angtbl_a_native);
  spyro::installNativeOverride(core, 0x80016C58u, "angtblB", angtbl_b_native);
  spyro::installNativeOverride(core, 0x80017928u, "angdist", angdist_native);
  spyro::installNativeOverride(core, 0x800176C8u, "vsra", vsra_native);
}
