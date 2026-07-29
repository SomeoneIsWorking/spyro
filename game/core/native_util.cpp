// native_util.cpp — small engine utilities this port owns outright: a set-and-return-previous global,
// a 2D distance approximation, and the display-list link primitive.
//
// Picked with tools/own_candidates.py by caller count among LEAF functions, same basis as
// native_leaf.cpp and native_angle.cpp. Ownership here buys architecture, not speed — a host profile
// puts ALL recompiled guest code under 5% of this port's CPU time (C082).
//
// EVERY REGISTER THE BODY LEAVES IS PART OF THE CONTRACT, including scratch ones no caller can read.
// Two of the three below leave $at holding an intermediate, and both reproduce it: the
// differential compares all 31 GPRs, so a body allowed to differ "where it cannot matter" makes the
// differential meaningless. This port has been bitten by exactly that once ($at, I019) and again by a
// register holding a loop value one iteration stale (spin60, C107).
//
// DELAY SLOTS DECIDE THE EXIT STATE. dl_link's pointer store is a branch delay slot, so the new node
// becomes the list head on BOTH paths — reading it as belonging to the not-taken arm gives a body that
// links correctly only when the list is empty.
#include "core.h"
#include "recomp_iface.h"
#include "rec_decls.h"
#include "native_diff.h"
#include "spyro_game.h"

namespace {

// 0x8006276C (strlen, 9 callers) and 0x80067614 (a set-and-return-previous global, 8 callers) were
// transcribed too, and are deliberately NOT installed: across 4000 frames with input driven, neither
// is called even once, so neither can be differentially verified. An override that has never been
// checked against the recompiled body is an unverified replacement of guest behaviour on a live path.
// Both transcriptions are recorded in the issue catalog ("transcribed but unexercised") so they can be
// installed and verified in a single step once a run reaches code that calls them.

// ── 0x80063C30 — set a global, return its PREVIOUS value. 7 static callers.
//     lui v0,0x8007 ; lw v0,0x4E38(v0) ; lui at,0x8007 ; sw a0,0x4E38(at) ; jr ra
// The load of the old value precedes the store, so this is a swap rather than a plain setter. The
// address is formed TWICE, by two separate luis, so $at exits holding 0x80070000 — the bare lui
// result, not the full address. Reproducing the address instead would diverge.
constexpr uint32_t kG3C30 = 0x80074E38u;
void setg_3c30_native(Core* c) {
  c->r[2] = c->mem_r32(kG3C30);    // v0 = old
  c->mem_w32(kG3C30, c->r[4]);
  c->r[1] = 0x80070000u;           // at — the lui, not the address
}

// ── 0x80017990 — approximate 2D distance between two points. 9 static callers.
//     dx = |[a1+0] - [a0+0]|, dy = |[a1+4] - [a0+4]|,  result = max + (3*min >> 3)
// The classic octagonal approximation: exact on the axes, ~6% high on the diagonal, and no square
// root. The two arms are mirror images — whichever of dx/dy is SMALLER is replaced by (3*min)>>3 and
// the larger is left alone, then the two are added.
//
// The shift is srl (LOGICAL), which is safe only because both values are already absolute. at exits
// as 2*min on both arms: it holds dx-dy at the comparison, then each arm overwrites it with the
// doubled minimum on the way to computing 3*min.
void dist2d_native(Core* c) {
  const uint32_t a0 = c->r[4], a1 = c->r[5];
  const uint32_t x0 = c->mem_r32(a0 + 0), y0 = c->mem_r32(a0 + 4);
  uint32_t dx = c->mem_r32(a1 + 0) - x0;
  uint32_t dy = c->mem_r32(a1 + 4) - y0;
  if ((int32_t)dx < 0) dx = 0u - dx;          // `sub a2,zero,a2`, so INT_MIN maps to itself
  if ((int32_t)dy < 0) dy = 0u - dy;
  uint32_t at;
  if ((int32_t)(dx - dy) >= 0) { at = dy << 1; dy = (dy + at) >> 3; }   // dx is the max
  else                         { at = dx << 1; dx = (dx + at) >> 3; }   // dy is the max
  c->r[1] = at;                    // at  — 2*min
  c->r[6] = dx;                    // a2
  c->r[7] = dy;                    // a3
  c->r[2] = dx + dy;               // v0
}

// ── 0x800168DC — link a node into the display list. 16 static callers, and it writes a 24-BIT pointer.
//     v1 = [0x8007581C] ; at = [v1] ; beq at,zero,EMPTY ; sw a0,0(v1)
//     sh a0,0(at) ; srl a0,a0,16 ; jr ra ; sb a0,2(at)      EMPTY: jr ra ; sw a0,4(v1)
// [0x8007581C] points at a small header; its first word is the current head. The new node becomes the
// head UNCONDITIONALLY, because that store is the branch's delay slot. What differs is where the OLD
// head learns about it: an empty list records the node at header+4, a non-empty one writes the node's
// address into the old head's low three bytes — halfword then byte, the PSX display-list convention of
// packing a pointer into 24 bits and leaving the 4th byte for a length or code.
//
// a0 SURVIVES SHIFTED on the non-empty path (srl by 16, to position the top byte for the sb) and
// unshifted on the empty one. That asymmetry is the whole reason this needs the differential rather
// than a careful read.
constexpr uint32_t kDlHead = 0x8007581Cu;
void dl_link_native(Core* c) {
  const uint32_t node = c->r[4];
  const uint32_t hdr = c->mem_r32(kDlHead);
  const uint32_t old = c->mem_r32(hdr);
  c->mem_w32(hdr, node);           // delay slot — the new node is the head either way
  c->r[3] = hdr;                   // v1
  c->r[1] = old;                   // at
  if (old == 0) {
    c->mem_w32(hdr + 4, node);     // empty list: recorded at header+4, a0 untouched
  } else {
    c->mem_w16(old + 0, (uint16_t)node);
    c->r[4] = node >> 16;          // a0 is shifted BEFORE the byte store
    c->mem_w8(old + 2, (uint8_t)(node >> 16));
  }
}

void setg3c30_owned (Core* c) { ndiff_run(c, "setg3c30@0x80063C30", setg_3c30_native, gen_func_80063C30); }
void dist2d_owned   (Core* c) { ndiff_run(c, "dist2d@0x80017990",   dist2d_native,   gen_func_80017990); }
void dllink_owned   (Core* c) { ndiff_run(c, "dllink@0x800168DC",   dl_link_native,  gen_func_800168DC); }

}  // namespace

void spyro_register_native_util() {
  psxport_recomp()->shard_set_override(0x80063C30u, setg3c30_owned);
  psxport_recomp()->shard_set_override(0x80017990u, dist2d_owned);
  psxport_recomp()->shard_set_override(0x800168DCu, dllink_owned);
}
