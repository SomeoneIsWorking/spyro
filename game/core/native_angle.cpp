// native_angle.cpp — the engine's angle helper and its calibrated spin, owned outright.
//
// Picked with tools/own_candidates.py by caller count among LEAF functions. A leaf's whole effect is
// registers plus memory, which is exactly what the per-call differential compares, so a leaf can be
// replaced outright rather than observed. Nothing here is chosen for speed: a host profile puts ALL
// recompiled guest code at under 5% of this port's CPU time (C082), so ownership buys architecture and
// correctness, not frames.
//
// EVERY REGISTER THE BODY LEAVES IS PART OF THE CONTRACT. Both bodies below leave a scratch register
// holding an intermediate no caller can legitimately read, and both reproduce it anyway: a replacement
// allowed to differ "where it cannot matter" makes the differential meaningless, and this port has
// already been bitten once by exactly that ($at, I019). PSXPORT_NDIFF checks the claim on real calls
// rather than trusting this comment — and it earned its keep here, catching spin60's v1 on call #1.
//
// DELAY SLOTS DECIDE THE EXIT STATE, and they are where transcription goes wrong. In the angle helper
// the instruction after the branch is NOT part of the taken path — it runs either way, so the register
// it writes holds the same value on both exits. Reading it as if the branch skipped it gives a body
// that matches on one path and diverges on the other.
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "spyro_game.h"

namespace {

// ── 0x80017908 — shortest ABSOLUTE separation of two 8-bit angles. 14 static callers.
//     sub a0,a0,a1 ; andi a0,a0,0xFF ; addi a1,a0,-128 ; bltz a1,L ; addi a1,zero,256
//     sub a0,a1,a0 ; L: jr ra ; addi v0,a0,0
// d = (a0-a1) & 0xFF, then the half of the circle it falls in decides the sign: d < 128 is already the
// short way round, otherwise the short way is 256-d. Result in v0 AND in a0 (the body computes in a0).
//
// a1 EXITS AS 256 ON BOTH PATHS. `addi a1,zero,256` is the delay slot of the bltz, so it runs whether
// or not the branch is taken — it is not the "d >= 128" arm setting up its subtraction, even though it
// reads exactly like that. The comparison value a1 held (d-128) is gone by the time either path exits.
void angdiff8_native(Core* c) {
  const int32_t d = (int32_t)((c->r[4] - c->r[5]) & 0xFFu);
  const int32_t v = (d - 128 < 0) ? d : 256 - d;
  c->r[4] = (uint32_t)v;    // a0 — the body's accumulator, left holding the result
  c->r[5] = 256u;           // a1 — the delay slot, on both paths
  c->r[2] = (uint32_t)v;    // v0
}

// 0x8001796C — the 12-bit signed angle wrap — is NOT owned here, deliberately. It transcribes
// cleanly (see docs/issues, 'angwrap12'), but across 4000 frames it is never once called: its 11
// static callers all sit on gameplay paths this port cannot yet drive headlessly (no input reaches
// gameplay — issue 0027). Installing it would put an unverified replacement of guest behaviour on a
// live path, and "it obviously transcribes correctly" is exactly the confidence the differential
// exists to check. It goes in when a run can actually exercise it.

// ── 0x8005C720 — a calibrated BUSY-WAIT. 18 static callers, the most of any leaf left in the image.
//     sp-=8 ; [sp+4]=13 ; [sp]=0 ; loop 60 times { [sp+4] *= 13 ; [sp]++ } ; sp+=8 ; ret
// The multiply is built from shifts and adds (v*2+v = 3v, <<2 = 12v, +v = 13v) and the product is never
// read by anything. This is a delay loop: its only purpose is to burn a fixed number of cycles, and 13
// is chosen because it makes a cheap shift-add multiply, not because the value means anything.
//
// A PC port should not burn CPU spinning, and owning this is how that stops — but the replacement is
// still BYTE-EXACT, because "the result is never read" is a claim about the guest, not about the
// differential. The two words live BELOW sp at exit (sp is restored before returning), so they are
// dead stack — and the differential compares all of RAM regardless, so a body that skipped the stores
// would diverge on every call. Reproduce them, and the loop itself collapses to the closed form.
//
// 13^60 mod 2^32 is computed here, not hardcoded: a magic constant standing in for a loop is exactly
// the kind of thing that is unreviewable later, and the arithmetic is free at this size.
// v1 is LOADED AT THE TOP of the body, so it lags the stored product by one multiply: on the final
// iteration it holds 13^60 while [sp+4] ends at 13^61. Setting both to the final product diverged on
// call #1 (native v1=0x890E6FBD vs substrate 0x94639271) — the stack words were already right, so this
// was purely the off-by-one-iteration in a register no caller reads. Which is the point of checking it.
void spin60_native(Core* c) {
  uint32_t v = 13u, loaded = 13u;
  for (int i = 0; i < 60; i++) { loaded = v; v *= 13u; }
  const uint32_t sp = c->r[29] - 8u;   // the frame the body allocates and then pops
  c->mem_w32(sp + 4, v);               // 13^61
  c->mem_w32(sp + 0, 60u);             // the loop counter, at its terminating value
  c->r[2] = 0u;                        // v0 — the slti that failed the loop test
  c->r[3] = loaded;                    // v1 — 13^60, the last value read from [sp+4]
}

void angdiff8_owned (Core* c) { ndiff_run(c, "angdiff8@0x80017908",  angdiff8_native,  gen_func_80017908); }
void spin60_owned   (Core* c) { ndiff_run(c, "spin60@0x8005C720",    spin60_native,    gen_func_8005C720); }

}  // namespace

void spyro_register_native_angle() {
  psxport_recomp()->shard_set_override(0x80017908u, angdiff8_owned);
  psxport_recomp()->shard_set_override(0x8005C720u, spin60_owned);
}
