// Hot LEAF functions this port owns outright: a leaf's whole effect is registers plus memory,
// which is exactly what the per-call differential compares, and every register it leaves —
// including ones that "cannot matter" — is part of the contract.
#include "core.h"
#include "native_execution.h"
#include "spyro_game.h"

namespace {

// 0x80017700 — copy three words (a VECTOR/SVECTOR copy).
//     lw at,0(a1) ; lw v0,4(a1) ; lw v1,8(a1) ; sw at,0(a0) ; sw v0,4(a0) ; jr ra ; sw v1,8(a0)
// The three loaded words are LEFT LIVE in at/v0/v1, because the differential compares them.
void copy3_native(Core *c) {
  const uint32_t dst = c->r[4], src = c->r[5];
  const uint32_t w0 = c->mem_r32(src + 0);
  const uint32_t w1 = c->mem_r32(src + 4);
  const uint32_t w2 = c->mem_r32(src + 8);
  c->mem_w32(dst + 0, w0);
  c->mem_w32(dst + 4, w1);
  c->mem_w32(dst + 8, w2);
  c->r[1] = w0; // at
  c->r[2] = w1; // v0
  c->r[3] = w2; // v1
}

// 0x800176F0 — zero three words.
//     sw zero,0(a0) ; sw zero,4(a0) ; jr ra ; sw zero,8(a0)
// Touches no register at all, which is itself the contract.
void zero3_native(Core *c) {
  const uint32_t dst = c->r[4];
  c->mem_w32(dst + 0, 0);
  c->mem_w32(dst + 4, 0);
  c->mem_w32(dst + 8, 0);
}

// 0x80016914 — fill a2 BYTES at a0 with the word a1.
//     add a2,a2,a0 ; addi a2,a2,-4 ; loop: sw a1,0(a0) ; bne a0,a2,loop ; addi a0,a0,4
//
// `bne` compares a0 BEFORE the delay-slot increment, and that increment runs on both the taken
// and the not-taken path, so this is a do-while: store at a0, advance, stop once the
// pre-increment a0 equalled the end.
//
// Length 0 is not guarded: the end address is a0-4, the first store still happens and the loop runs
// away, which is what the executable does.
void fill_native(Core *c) {
  const uint32_t start = c->r[4], val = c->r[5], len = c->r[6];
  const uint32_t end = start + len - 4; // final a2
  uint32_t p = start;
  for (;;) {
    c->mem_w32(p, val);
    const bool more = (p != end);
    p += 4; // the delay slot: always executed
    if (!more) {
      break;
    }
  }
  c->r[4] = p;   // a0 advanced past the last store
  c->r[6] = end; // a2 as the body leaves it
}

// 0x80016958 — copy a2 BYTES from a1 to a0, four words per pass.
//     a2 = a0 + len - 4                     the LAST destination address
//     loop: load four words from a1 ; a1 += 16
//           store one, `beq a0,a2` exit, `addi a0,a0,4` in the delay slot — four times
//
// Two behaviours that look like bugs and are not:
//   * a1 advances by 16 at the TOP of every pass, before any store, so a copy ending mid-pass
//   leaves
//     a1 further along than the bytes copied.
//   * each `beq` compares a0 BEFORE its delay-slot increment, so the loop stores AT the end address
//     and exits with a0 one word past it.
void copy_words_native(Core *c) {
  uint32_t dst = c->r[4], src = c->r[5];
  const uint32_t end = dst + c->r[6] - 4u;
  uint32_t w[4] = {0, 0, 0, 0};
  for (;;) {
    w[0] = c->mem_r32(src + 0);
    w[1] = c->mem_r32(src + 4);
    w[2] = c->mem_r32(src + 8);
    w[3] = c->mem_r32(src + 12);
    src += 16; // before any store, exactly as the body does it
    bool done = false;
    for (int i = 0; i < 4; i++) {
      c->mem_w32(dst, w[i]);
      done = (dst == end);
      dst += 4; // the delay slot: always executed
      if (done) {
        break;
      }
    }
    if (done) {
      break;
    }
  }
  c->r[4] = dst;
  c->r[5] = src;
  c->r[6] = end;
  c->r[10] = w[0];
  c->r[11] = w[1];
  c->r[12] = w[2];
  c->r[13] = w[3]; // t2..t5
}

} // namespace

void spyro::registerNativeLeaves(Core &core) {
  psx::cpu::installNativeOverride(core, 0x80017700u, "copy3", copy3_native);
  psx::cpu::installNativeOverride(core, 0x800176F0u, "zero3", zero3_native);
  psx::cpu::installNativeOverride(core, 0x80016914u, "fill", fill_native);
  psx::cpu::installNativeOverride(core, 0x80016958u, "copyw", copy_words_native);
}
