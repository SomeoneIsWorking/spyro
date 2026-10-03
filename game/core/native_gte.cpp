// native_gte.cpp — geometry leaves that use the GTE (COP2).
//
// OWNING GTE CODE WITHOUT REIMPLEMENTING THE GTE. The scalar logic around a GTE command is ordinary
// code and belongs to the port; the command itself is HARDWARE and belongs to the platform layer.
// So the native body does the loads, the arithmetic and the table lookup itself, and calls the
// framework's own gte_op()/gte_read_data()/gte_write_data() for the COP2 work — the same model the
// runtime uses, so those results match by construction rather than by re-deriving Beetle's
// saturation and flag rules. Reimplementing the GTE here would be a large, subtle piece of work
// with no benefit: it is the platform's job, not the game's.
//
// This was only verifiable once the differential learned to compare COP2 state. Before that
// it would have reported "matches" on a body that left IR/MAC/FLAGS different, none of which is
// guest RAM.
#include "core.h"
#include "game.h"
#include "guest_globals.h"
#include "guest_magnitude.h"
#include "native_execution.h"
#include "spyro_game.h"

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
// The SQRT TAIL, shared by 0x800171FC, 0x80017330 and 0x80017A38. All three take a non-negative
// value, use the GTE's leading-zero counter to normalise it, index the table at 0x80074B84 and
// shift back. The bodies differ ONLY in which registers hold the input and the shift amount
// (veclen: at/a0; scale-to-length: a2; isqrt: a0/at), so the arithmetic is factored and each
// caller places the results in its own registers. Duplicating it would mean a fix to one and not
// the other, which is the likelier future bug.
struct SqrtTail {
  uint32_t v0, a1, shift, a2, a3, t0;
};

SqrtTail sqrt_tail(Core *c, uint32_t val) {
  // LZCR — the mtc2 to LZCS already happened.
  const auto step = spyro::guest_magnitude::normalize(val, gte_read_data(31));
  SqrtTail r{};
  r.a1 = step.evenLeadingZeros;
  r.shift = step.exponent;
  r.a2 = step.residualShift;
  r.a3 = step.tableByteOffset;
  r.t0 = spyro::guest_magnitude::scaled((int16_t)c->mem_r16(spyro::guest::kMagnitudeTable + r.a3),
                                        r.shift);
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
// The register PERMUTATION and the sign flips are recovered exactly rather than rationalised into
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

// ── 0x800170C0 — transform a point at a0 into a1 through the matrix the GTE ALREADY holds.
//     lw at/v0/v1,0..8(a0)  ; mtc2 at->IR3 ; v0 = -v0 -> IR1 ; v1 = -v1 -> IR2
//     nop ; nop            the MVMVA latency, and the only difference from 0x80017048
//     GTE 0x4A49E012 (MVMVA) ; mfc2 at<-MAC3, v0<-MAC1, v1<-MAC2 ; negate v0,v1 ; store to a1
// The same MVMVA tail as 0x80017048 with the matrix load left out, so the CR0..CR4 the command
// reads are whatever the resident matrix left there — which is the point: this is the entry a
// caller reaches when it has already built the matrix, and a body that loaded one here would
// transform by the wrong matrix. The register permutation and both sign flips are the sibling's.
void mvmva_resident_native(Core *c) {
  const uint32_t v = c->r[4], out = c->r[5];

  gte_write_data(11, c->mem_r32(v + 0));      // IR3
  gte_write_data(9, 0u - c->mem_r32(v + 4));  // IR1, negated
  gte_write_data(10, 0u - c->mem_r32(v + 8)); // IR2, negated

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

// ── 0x80017110 — transform the point at a0 into a1 through the matrix g_Camera already holds.
//     lw a2/a3/t0/t1/t2, 0x14..0x24(at) with at = g_Camera    the five matrix words themselves
//     mtc2 a2/a3/t0/t1/t2 -> CR0..CR4                       the same CR0..CR4 as 0x80017048
//     lw at/v0/v1,0..8(a0) ; mtc2 -> IR3 ; v0 = -v0 -> IR1 ; v1 = -v1 -> IR2 ; nop ; nop
//     GTE 0x4A49E012 (MVMVA) ; mfc2 at<-MAC1, v0<-MAC2, v1<-MAC3 ; store to a1
// The sibling of 0x800170C0 with the matrix load put back — the only difference between the two
// bodies is these five words and the result permutation, which is the OTHER way round here: the
// first MAC lands in $at and is stored unnegated, where 0x800170C0 puts MAC3 in $at and negates
// MAC1/MAC2. Reading either body as the other writes a different vector, so the permutation is
// reproduced rather than tidied.
constexpr std::uint32_t kCameraMatrix = spyro::guest::kCamera + 0x14u; // 0x80076DE4

void mvmva_camera_matrix_native(Core *c) {
  const std::uint32_t point = c->r[4], out = c->r[5];

  gte_write_ctrl(0, c->mem_r32(kCameraMatrix + 0));
  gte_write_ctrl(1, c->mem_r32(kCameraMatrix + 4));
  gte_write_ctrl(2, c->mem_r32(kCameraMatrix + 8));
  gte_write_ctrl(3, c->mem_r32(kCameraMatrix + 12));
  gte_write_ctrl(4, c->mem_r32(kCameraMatrix + 16));

  const std::uint32_t x = c->mem_r32(point + 0);
  const std::uint32_t y = 0u - c->mem_r32(point + 4);
  const std::uint32_t z = 0u - c->mem_r32(point + 8);
  gte_write_data(11, x); // IR3
  gte_write_data(9, y);  // IR1
  gte_write_data(10, z); // IR2

  gte_op(c, 0x4A49E012u); // MVMVA

  const std::uint32_t r_at = gte_read_data(25); // MAC1
  const std::uint32_t r_v0 = gte_read_data(26); // MAC2
  const std::uint32_t r_v1 = gte_read_data(27); // MAC3
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

// The guest's `mult`/`mflo`/`sra 12`: a signed 64-bit product whose LOW word is shifted back
// down. Only the low word reaches memory, but hi and lo are state the differential compares, and
// the last multiply of a body is the one that survives — so both words are written on every call,
// and the caller runs its three in guest order.
uint32_t scaled_component(Core *c, uint32_t component, uint32_t factor) {
  const int64_t product = (int64_t)(int32_t)component * (int64_t)(int32_t)factor;
  const uint32_t low = (uint32_t)(product & 0xFFFFFFFFu);
  c->lo = low;
  c->hi = (uint32_t)((uint64_t)product >> 32);
  return (uint32_t)((int32_t)low >> 12); // sra
}

// ── 0x80017330 — scale the 3-vector at a0 to length a1, in place.
//     lw at/v0/v1,0..8(a0) ; mtc2 -> IR1..3 ; GTE 0x4AA00428 (SQR) ; mfc2 -> MAC1..3
//     add a2,a2,a3 ; add a2,a2,t0        a2 = the sum of the three squares
//     mtc2 a2,$30 (LZCS) ; beqz a2,ZEROS ; mfc2 a3,$31 (LZCR)
//     <the shared tail> -> a2 = the divisor ; sll a1,a1,12 ; div zero,a1,a2 ; mflo a2
//     mult/mflo/sra 12 per component ; sw back to a0
// The tail is veclen's and isqrt's verbatim — same LZCR count, same table, same shifts — so it
// comes from sqrt_tail instead of a third copy. A ZERO SUM EXITS BEFORE THE DIVIDE, so v0 and v1
// leave holding y and z exactly as loaded, unscaled, and hi/lo are never written on that arm at
// all; the zero arm's third store is the `jr $ra` delay slot, so it happens there too.
void vec_scale_to_length_native(Core *c) {
  const uint32_t vector = c->r[4];
  const uint32_t x = c->mem_r32(vector + 0);
  const uint32_t y = c->mem_r32(vector + 4);
  const uint32_t z = c->mem_r32(vector + 8);

  gte_write_data(9, x);
  gte_write_data(10, y);
  gte_write_data(11, z);

  gte_op(c, 0x4AA00428u); // SQR

  const uint32_t sum = gte_read_data(25) + gte_read_data(26) + gte_read_data(27);
  gte_write_data(30, sum); // LZCS — the count happens on the write, before the branch
  if (sum == 0) {
    c->mem_w32(vector + 0, 0);
    c->mem_w32(vector + 4, 0);
    c->mem_w32(vector + 8, 0);
    c->r[2] = y; // v0 — the component as loaded
    c->r[3] = z; // v1
    return;
  }

  const SqrtTail r = sqrt_tail(c, sum);
  cpu_div(c, c->r[5] << 12, r.v0); // sll a1,12 then the signed divide
  const uint32_t scale = c->lo;    // mflo

  const uint32_t scaledX = scaled_component(c, x, scale);
  const uint32_t scaledY = scaled_component(c, y, scale);
  const uint32_t scaledZ = scaled_component(c, z, scale);
  c->mem_w32(vector + 0, scaledX);
  c->mem_w32(vector + 4, scaledY);
  c->mem_w32(vector + 8, scaledZ);
  c->r[2] = scaledY; // v0
  c->r[3] = scaledZ; // v1
}

// ── 0x800177C0 — out = a2 * the 3-vector at a1: the multiply-and-accumulate half of the pair
//     with 0x800175B8, which divides instead.
//     lwc2 $9/$10/$11,0..8(a1)   IR1..3 = the vector
//     mtc2 zero,$25/$26/$27      MAC1..3 = 0 — GPL ADDS to the accumulators, so clear them first
//     mtc2 a2,$8                IR0 = the scalar
//     GTE 0x4BA0003E (GPL)       MAC1..3 = the fixed-point product, saturated by the hardware
//     swc2 $25/$26/$27 -> a0
// v0 and v1 are left untouched: the body never writes them, and the third store is the `jr $ra`
// delay slot, so all three words are written on every path.
void vec_mul_scalar_native(Core *c) {
  const uint32_t out = c->r[4];
  const uint32_t vector = c->r[5];

  gte_write_data(9, c->mem_r32(vector + 0));
  gte_write_data(10, c->mem_r32(vector + 4));
  gte_write_data(11, c->mem_r32(vector + 8));
  gte_write_data(25, 0);
  gte_write_data(26, 0);
  gte_write_data(27, 0);
  gte_write_data(8, c->r[6]);

  gte_op(c, 0x4BA0003Eu); // GPL

  c->mem_w32(out + 0, gte_read_data(25));
  c->mem_w32(out + 4, gte_read_data(26));
  c->mem_w32(out + 8, gte_read_data(27));
}

} // namespace

void spyro::registerNativeGte(Core &core) {
  spyro::installNativeOverride(core, 0x800171FCu, "veclen", veclen_native);
  spyro::installNativeOverride(core, 0x80017048u, "mvmva", mvmva_native);
  spyro::installNativeOverride(core, 0x800170C0u, "mvmva_resident", mvmva_resident_native);
  spyro::installNativeOverride(
      core, 0x80017110u, "mvmva_camera_matrix", mvmva_camera_matrix_native);
  spyro::installNativeOverride(core, 0x80017A38u, "isqrt", isqrt_native);
  spyro::installNativeOverride(core, 0x800175B8u, "vscale", vscale_native);
  spyro::installNativeOverride(
      core, 0x80017330u, "vec_scale_to_length", vec_scale_to_length_native);
  spyro::installNativeOverride(core, 0x800177C0u, "vec_mul_scalar", vec_mul_scalar_native);
}
