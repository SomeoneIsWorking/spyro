// native_gte.cpp — geometry leaves that use the GTE (COP2).
//
// OWNING GTE CODE WITHOUT REIMPLEMENTING THE GTE. The scalar logic around a GTE command is ordinary
// code and belongs to the port; the command itself is HARDWARE and belongs to the platform layer. So
// the native body does the loads, the arithmetic and the table lookup itself, and calls the
// framework's own gte_op()/gte_read_data()/gte_write_data() for the COP2 work — the same model the
// substrate uses, so those results match by construction rather than by my re-deriving Beetle's
// saturation and flag rules. Reimplementing the GTE here would be a large, subtle piece of work with
// no benefit: it is the platform's job, not the game's.
//
// This was only verifiable once the differential learned to compare COP2 state (I021). Before that it
// would have reported "matches" on a body that left IR/MAC/FLAGS different, none of which is guest
// RAM.
#include "core.h"
#include "game.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "spyro_game.h"

// The GTE accessors are FREE functions bound to the current core's register file (gte_bind), not Core
// members — the same ones the generated shards call, which is exactly why results match by
// construction rather than by my re-deriving the model.
uint32_t gte_read_data(uint32_t reg);
void     gte_write_data(uint32_t reg, uint32_t v);

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
void veclen_native(Core* c) {
  const uint32_t p = c->r[4], with_z = c->r[5];

  gte_write_data(9, c->mem_r32(p + 0));               // IR1 = x
  gte_write_data(11, 0);                              // IR3 = 0
  gte_write_data(10, c->mem_r32(p + 4));              // IR2 = y — the delay slot, unconditional
  if (with_z != 0) gte_write_data(11, c->mem_r32(p + 8));

  gte_op(c, 0x4AA00428u);                             // SQR

  const uint32_t mac1 = gte_read_data(25);
  const uint32_t mac2 = gte_read_data(26);
  const uint32_t mac3 = gte_read_data(27);
  const uint32_t sum = mac1 + mac2 + mac3;

  gte_write_data(30, sum);                            // LZCS — triggers the count
  c->r[1] = sum;                                      // at
  c->r[3] = mac3;                                     // v1

  if (sum == 0) {
    // The zero arm: t0 = 0 from the delay slot, v0 = 0 from the return delay slot. a0/a1 keep the
    // INCOMING argument values because the normalisation below never runs, and v0's mfc2 of LZCR is
    // after the branch so it never happens either.
    c->r[8] = 0;                                      // t0
    c->r[2] = 0;                                      // v0 = t0 >> 12
    return;
  }

  const uint32_t lzc = gte_read_data(31);             // LZCR
  const uint32_t a1v = lzc & ~1u;                     // `addi a1,zero,-2 ; and a1,v0,a1`
  const uint32_t a0v = (uint32_t)((int32_t)(31 - (int32_t)a1v) >> 1);   // sra
  const int32_t a2s = (int32_t)a1v - 24;

  uint32_t a3v, a2f;
  if (a2s < 0) {
    a2f = 24u - a1v;                                  // branch taken: addi 24 then sub
    a3v = (uint32_t)((int32_t)sum >> (int)(a2f & 31u));   // srav — ARITHMETIC
  } else {
    a3v = sum << ((uint32_t)a2s & 31u);               // sllv
    a2f = 24u;                                        // the j's delay slot, on this path only
  }
  a3v = (a3v - 64u) << 1;

  const uint32_t ent = 0x80074B84u + a3v;             // lui 0x8007 ; addiu 19332
  const uint32_t t0v = (uint32_t)(int32_t)(int16_t)c->mem_r16(ent) << (a0v & 31u);

  c->r[2] = t0v >> 12;                                // v0 — srl, LOGICAL
  c->r[4] = a0v;                                      // a0 was overwritten by the normalisation
  c->r[5] = a1v;                                      // a1 likewise
  c->r[6] = a2f;                                      // a2
  c->r[7] = a3v;                                      // a3
  c->r[8] = t0v;                                      // t0
}

void veclen_owned(Core* c) { ndiff_run(c, "veclen@0x800171FC", veclen_native, gen_func_800171FC); }

}  // namespace

void spyro_register_native_gte() {
  psxport_recomp()->shard_set_override(0x800171FCu, veclen_owned);
}
