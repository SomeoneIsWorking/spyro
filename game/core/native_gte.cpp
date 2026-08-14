// native_gte.cpp — geometry leaves that use the GTE (COP2).
//
// OWNING GTE CODE WITHOUT REIMPLEMENTING THE GTE. The scalar logic around a GTE command is ordinary
// code and belongs to the port; the command itself is HARDWARE and belongs to the platform layer.
// So the native body does the loads, the arithmetic and the table lookup itself, and calls the
// framework's own gte_op()/gte_read_data()/gte_write_data() for the COP2 work — the same model the
// substrate uses, so those results match by construction rather than by my re-deriving Beetle's
// saturation and flag rules. Reimplementing the GTE here would be a large, subtle piece of work
// with no benefit: it is the platform's job, not the game's.
//
// This was only verifiable once the differential learned to compare COP2 state (I021). Before that
// it would have reported "matches" on a body that left IR/MAC/FLAGS different, none of which is
// guest RAM.
#include "core.h"
#include "game.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"

// The GTE accessors are FREE functions bound to the current core's register file (gte_bind), not
// Core members — the same ones the generated shards call, which is exactly why results match by
// construction rather than by my re-deriving the model.
uint32_t gte_read_data(uint32_t reg);
void gte_write_data(uint32_t reg, uint32_t v);
void gte_write_ctrl(uint32_t reg, uint32_t v);
void cpu_div(Core *c, uint32_t n, uint32_t d);

namespace {

// ── 0x800171FC — length of a 3-vector (or of its XY only, when a1 == 0). 87 static callers, the
// biggest remaining candidate in the image.
//
//   lwc2  $9,0(a0)            IR1 = v.x
//   mtc2  zero,$11            IR3 = 0
//   beq   a1,zero,+2          ; a1 == 0 -> leave IR2 untouched, IR3 zero (2-D length)
//   lwc2  $10,4(a0)           ;   DELAY SLOT — IR2 = v.y happens EITHER WAY
//   lwc2  $11,8(a0)           IR3 = v.z            (skipped when a1 == 0)
//   GTE   0x4AA00428          SQR(sf=0,lm=1): MAC1..3 = IR1..3 squared
//   mfc2  at,$25 / v0,$26 / v1,$27      MAC1, MAC2, MAC3
//   add   at,at,v0 ; add at,at,v1       at = x^2 + y^2 + z^2
//   mtc2  at,$30              LZCS — writing it computes the leading-zero count
//   beq   at,zero,end ; addi t0,zero,0  ; DELAY SLOT — t0 = 0 either way
//   mfc2  v0,$31              LZCR = leading zeros of the sum
//   ...normalise, look up 0x80074B84[], shift...
//   jr ra ; srl v0,t0,12      ; DELAY SLOT
//
// THE TWO DELAY SLOTS ARE THE WHOLE FUNCTION'S SUBTLETY, and both are easy to read wrong:
//   * `lwc2 $10,4(a0)` sits in the branch's delay slot, so IR2 is loaded even when a1 == 0. The
//     2-D case is "z excluded", NOT "y and z excluded".
//   * at 0x80017264 the instruction at 0x80017268 is simultaneously the `j`'s delay slot AND the
//     `bltz`'s target. Taken: a2 = 24 then `sub a2,a2,a1` -> a2 = 24 - a1, and the value is shifted
//     RIGHT. Not taken: the shift is LEFT by a1-24 and a2 ends at 24, with `sub` skipped entirely.
// The SQRT TAIL, shared by 0x800171FC and 0x80017A38. Both take a non-negative value, use the GTE's
// leading-zero counter to normalise it, index the table at 0x80074B84 and shift back. The two
// bodies differ ONLY in which registers hold the input and the shift amount (veclen: at/a0; isqrt:
// a0/at), so the arithmetic is factored and each caller places the results in its own registers.
// Duplicating it would mean a fix to one and not the other, which is the likelier future bug.
struct SqrtTail {
  uint32_t v0, a1, shift, a2, a3, t0;
};

SqrtTail sqrt_tail(Core *c, uint32_t val) {
  SqrtTail r{};
  const uint32_t lzc = gte_read_data(31); // LZCR — the mtc2 to LZCS already happened
  r.a1 = lzc & ~1u;
  r.shift = (uint32_t)((int32_t)(31 - (int32_t)r.a1) >> 1); // sra
  const int32_t a2s = (int32_t)r.a1 - 24;
  if (a2s < 0) {
    r.a2 = 24u - r.a1;                                    // branch taken: addi 24, then sub
    r.a3 = (uint32_t)((int32_t)val >> (int)(r.a2 & 31u)); // srav — ARITHMETIC
  } else {
    r.a3 = val << ((uint32_t)a2s & 31u); // sllv
    r.a2 = 24u;                          // the j's delay slot, this path only
  }
  r.a3 = (r.a3 - 64u) << 1;
  r.t0 = (uint32_t)(int32_t)(int16_t)c->mem_r16(0x80074B84u + r.a3) << (r.shift & 31u);
  r.v0 = r.t0 >> 12; // srl — LOGICAL
  return r;
}

// ── 0x80017A38 — integer square root of a0 via the same table. 17 static callers.
// Identical tail to veclen, but the value arrives in a0 and the shift amount lives in `at`.
void isqrt_native(Core *c) {
  const uint32_t val = c->r[4];
  gte_write_data(30, val); // LZCS
  if (val == 0) {
    c->r[8] = 0;
    c->r[2] = 0;
    return;
  } // t0 = 0 (delay slot), v0 = t0 >> 12
  const SqrtTail r = sqrt_tail(c, val);
  c->r[1] = r.shift; // at — here the shift amount, not the sum
  c->r[5] = r.a1;
  c->r[6] = r.a2;
  c->r[7] = r.a3;
  c->r[8] = r.t0;
  c->r[2] = r.v0;
}

void veclen_native(Core *c) {
  const uint32_t p = c->r[4], with_z = c->r[5];

  gte_write_data(9, c->mem_r32(p + 0));  // IR1 = x
  gte_write_data(11, 0);                 // IR3 = 0
  gte_write_data(10, c->mem_r32(p + 4)); // IR2 = y — the delay slot, unconditional
  if (with_z != 0) {
    gte_write_data(11, c->mem_r32(p + 8));
  }

  gte_op(c, 0x4AA00428u); // SQR

  const uint32_t mac1 = gte_read_data(25);
  const uint32_t mac2 = gte_read_data(26);
  const uint32_t mac3 = gte_read_data(27);
  const uint32_t sum = mac1 + mac2 + mac3;

  gte_write_data(30, sum); // LZCS — triggers the count
  c->r[1] = sum;           // at
  c->r[3] = mac3;          // v1

  if (sum == 0) {
    // The zero arm: t0 = 0 from the delay slot, v0 = 0 from the return delay slot. a0/a1 keep the
    // INCOMING argument values because the normalisation below never runs, and v0's mfc2 of LZCR is
    // after the branch so it never happens either.
    c->r[8] = 0; // t0
    c->r[2] = 0; // v0 = t0 >> 12
    return;
  }

  const SqrtTail r = sqrt_tail(c, sum);
  c->r[2] = r.v0;
  c->r[4] = r.shift; // a0 — overwritten by the normalisation
  c->r[5] = r.a1;
  c->r[6] = r.a2;
  c->r[7] = r.a3;
  c->r[8] = r.t0;
}

// ── 0x80017048 — load a 3x3 matrix from a0, transform the vector at a1, store to a2. 30 callers.
//     lw at/v0/v1,0..8(a0)  ; ctc2 -> CR0,CR1,CR2      the rotation matrix
//     lw at/v0,12..16(a0)   ; ctc2 -> CR3,CR4
//     lw at/v0/v1,0..8(a1)  ; mtc2 at->IR3 ; v0 = -v0 -> IR1 ; v1 = -v1 -> IR2
//     GTE 0x4A49E012 (MVMVA) ; mfc2 at<-MAC3, v0<-MAC1, v1<-MAC2 ; negate v0,v1 ; store to a2
// The register PERMUTATION and the sign flips are transcribed exactly rather than rationalised into
// whatever geometry they implement — getting the intent right is not the job, getting the bytes
// right is, and the differential checks the bytes.
void mvmva_native(Core *c) {
  const uint32_t m = c->r[4], v = c->r[5], out = c->r[6];

  gte_write_ctrl(0, c->mem_r32(m + 0));
  gte_write_ctrl(1, c->mem_r32(m + 4));
  gte_write_ctrl(2, c->mem_r32(m + 8));
  gte_write_ctrl(3, c->mem_r32(m + 12));
  gte_write_ctrl(4, c->mem_r32(m + 16));

  const uint32_t x = c->mem_r32(v + 0);
  const uint32_t y = 0u - c->mem_r32(v + 4);
  const uint32_t z = 0u - c->mem_r32(v + 8);
  gte_write_data(11, x); // IR3
  gte_write_data(9, y);  // IR1
  gte_write_data(10, z); // IR2

  gte_op(c, 0x4A49E012u); // MVMVA

  const uint32_t r_at = gte_read_data(27);      // MAC3
  const uint32_t r_v0 = 0u - gte_read_data(25); // MAC1, negated
  const uint32_t r_v1 = 0u - gte_read_data(26); // MAC2, negated
  c->mem_w32(out + 0, r_at);
  c->mem_w32(out + 4, r_v0);
  c->mem_w32(out + 8, r_v1);
  c->r[1] = r_at;
  c->r[2] = r_v0;
  c->r[3] = r_v1;
}

// ── 0x800175B8 — scale the vector at a0 by (a2 << 12) / a1, in place. 24 static callers.
//     sll a2,a2,12 ; div a2,a1          the divide runs BEFORE the GTE setup and sets hi/lo
//     lwc2 $9/$10/$11,0..8(a0)          IR1..3 = the vector
//     mtc2 zero,$25/$26/$27             MAC1..3 = 0
//     mflo a1 ; mtc2 a1,$8              IR0 = the quotient
//     GTE 0x4BA0003E (GPL)              MAC += IR0 * IR
//     mfc2 at/v0/v1,$25/$26/$27 ; sra 12 each ; store back to a0
//
// `div` is a SIGNED MIPS divide with its own defined no-trap behaviour for /0 and for
// INT_MIN / -1, so the platform's cpu_div is called rather than using C++ division — which is UB in
// exactly those two cases. It also leaves hi and lo set, and the differential compares both.
void vscale_native(Core *c) {
  const uint32_t p = c->r[4];
  const uint32_t a2v = c->r[6] << 12;
  c->r[6] = a2v;
  cpu_div(c, a2v, c->r[5]); // sets c->lo (quotient) and c->hi (remainder)

  gte_write_data(9, c->mem_r32(p + 0));
  gte_write_data(10, c->mem_r32(p + 4));
  gte_write_data(11, c->mem_r32(p + 8));
  gte_write_data(25, 0);
  gte_write_data(26, 0);
  gte_write_data(27, 0);

  const uint32_t q = c->lo; // mflo a1
  c->r[5] = q;
  gte_write_data(8, q); // IR0

  gte_op(c, 0x4BA0003Eu); // GPL

  const uint32_t r1 = (uint32_t)((int32_t)gte_read_data(25) >> 12); // sra 12
  const uint32_t r2 = (uint32_t)((int32_t)gte_read_data(26) >> 12);
  const uint32_t r3 = (uint32_t)((int32_t)gte_read_data(27) >> 12);
  c->mem_w32(p + 0, r1);
  c->mem_w32(p + 4, r2);
  c->mem_w32(p + 8, r3);
  c->r[1] = r1;
  c->r[2] = r2;
  c->r[3] = r3;
}

void veclen_owned(Core *c) {
  ndiff_run(c, "veclen@0x800171FC", veclen_native, gen_func_800171FC);
}
void vscale_owned(Core *c) {
  ndiff_run(c, "vscale@0x800175B8", vscale_native, gen_func_800175B8);
}
void mvmva_owned(Core *c) {
  ndiff_run(c, "mvmva@0x80017048", mvmva_native, gen_func_80017048);
}
void isqrt_owned(Core *c) {
  ndiff_run(c, "isqrt@0x80017A38", isqrt_native, gen_func_80017A38);
}

} // namespace

void spyro_register_native_gte() {
  psxport_recomp()->shard_set_override(0x800171FCu, veclen_owned);
  psxport_recomp()->shard_set_override(0x80017048u, mvmva_owned);
  psxport_recomp()->shard_set_override(0x80017A38u, isqrt_owned);
  psxport_recomp()->shard_set_override(0x800175B8u, vscale_owned);
}
