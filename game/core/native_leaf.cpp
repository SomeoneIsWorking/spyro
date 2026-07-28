// native_leaf.cpp — hot LEAF functions this port owns outright.
//
// Chosen with tools/own_candidates.py rather than by eye, after picking by eye went wrong: the buffer
// flip looked like a small, exactly-specified target because its first dozen instructions are a flip,
// and it continues into the entire per-frame stage dispatcher. Owning it would have meant owning the
// frame loop by accident.
//
// A LEAF (no jal/jalr) is the right shape for a first-class replacement: its whole effect is registers
// plus memory, which is exactly what the per-call differential compares. A non-leaf drags its callees
// into the replacement.
//
// EVERY REGISTER THE BODY LEAVES IS PART OF THE CONTRACT, including ones that "cannot matter". The
// first native function in this port matched for nine calls and then diverged on $at, which no
// compiler-generated code reads across a call (I019). Each body below therefore reproduces the exact
// final register state, and PSXPORT_NDIFF checks that claim on real calls rather than trusting this
// comment.
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "spyro_game.h"

namespace {

// ── 0x80017700 — copy three words (a VECTOR/SVECTOR copy). 136 static callers, the most in the image.
//     lw at,0(a1) ; lw v0,4(a1) ; lw v1,8(a1) ; sw at,0(a0) ; sw v0,4(a0) ; jr ra ; sw v1,8(a0)
// The three loaded words are LEFT LIVE in at/v0/v1 — reproduced, not because a caller reads them (none
// should) but because the differential compares them and a replacement allowed to differ "where it
// does not matter" makes the differential meaningless.
void copy3_native(Core* c) {
  const uint32_t dst = c->r[4], src = c->r[5];
  const uint32_t w0 = c->mem_r32(src + 0);
  const uint32_t w1 = c->mem_r32(src + 4);
  const uint32_t w2 = c->mem_r32(src + 8);
  c->mem_w32(dst + 0, w0);
  c->mem_w32(dst + 4, w1);
  c->mem_w32(dst + 8, w2);
  c->r[1] = w0;   // at
  c->r[2] = w1;   // v0
  c->r[3] = w2;   // v1
}

// ── 0x800176F0 — zero three words. 40 static callers.
//     sw zero,0(a0) ; sw zero,4(a0) ; jr ra ; sw zero,8(a0)
// Touches no register at all, which is itself the contract: a native body that helpfully clobbered a
// scratch register would diverge.
void zero3_native(Core* c) {
  const uint32_t dst = c->r[4];
  c->mem_w32(dst + 0, 0);
  c->mem_w32(dst + 4, 0);
  c->mem_w32(dst + 8, 0);
}

// ── 0x80016914 — fill a2 BYTES at a0 with the word a1. 59 static callers.
//     add a2,a2,a0 ; addi a2,a2,-4 ; loop: sw a1,0(a0) ; bne a0,a2,loop ; addi a0,a0,4
//
// Read the delay slot carefully, because it decides the exact bounds and the exact exit state. `bne`
// compares a0 BEFORE the delay-slot increment, and the delay slot runs on both the taken and the
// not-taken path. So this is a DO-WHILE: store at a0, then advance, and stop once the pre-increment
// a0 equalled the end. Stores land at a0 .. a0+len-4 inclusive (len/4 words), and on exit
// a2 = a0_initial + len - 4 while a0 = a0_initial + len.
//
// LENGTH 0 IS NOT GUARDED, deliberately. With a2 = 0 the end address is a0-4, the first store still
// happens, and the loop runs away — that is what the recompiled body does, so a native body that
// "helpfully" returned early would DIVERGE from the substrate on a caller bug and hide it. Faithful
// includes faithful to the broken case.
void fill_native(Core* c) {
  const uint32_t start = c->r[4], val = c->r[5], len = c->r[6];
  const uint32_t end = start + len - 4;          // final a2
  uint32_t p = start;
  for (;;) {
    c->mem_w32(p, val);
    const bool more = (p != end);
    p += 4;                                       // the delay slot: always executed
    if (!more) break;
  }
  c->r[4] = p;      // a0 advanced past the last store
  c->r[6] = end;    // a2 as the body leaves it
}

void copy3_owned(Core* c) { ndiff_run(c, "copy3@0x80017700", copy3_native, gen_func_80017700); }
void zero3_owned(Core* c) { ndiff_run(c, "zero3@0x800176F0", zero3_native, gen_func_800176F0); }
void fill_owned (Core* c) { ndiff_run(c, "fill@0x80016914",  fill_native,  gen_func_80016914); }

}  // namespace

void spyro_register_native_leaves() {
  psxport_recomp()->shard_set_override(0x80017700u, copy3_owned);
  psxport_recomp()->shard_set_override(0x800176F0u, zero3_owned);
  psxport_recomp()->shard_set_override(0x80016914u, fill_owned);
}
