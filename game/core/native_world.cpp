// native_world.cpp — Spyro's world renderer (0x800258F0 RenderWorldChunks), owned natively.
//
// WHY THIS FUNCTION, AND WHY OWNERSHIP. Widescreen and 60fps both need per-primitive DEPTH for
// 2D-vs-3D discrimination (re-frontier render.own-geometry-family): the framework's 2D widen shifts
// the whole frame a second time at this port's ~2.5% depth coverage (C143) and the uncovered-margin
// strip (issue 0039) is in the same class. Depth (SZ3) is a GTE/scratchpad intermediate ONLY in this
// renderer — never written into the emitted GPU packets (C198) — so an interpreted body cannot
// supply it; only an OWNED body can emit depth as it projects.
//
// THE SHAPE, from the structural map (C198) and the recompiled body (generated/shard_3.c
// gen_func_800258F0, ~5000 instructions across 0x800258F0..0x8002A6F4):
//   Phase 1 (0x800258F0..0x800261A0): save callee-saved regs to the fixed save area D_80077DD8
//     (no stack frame), load the camera view matrix into the GTE (translation nulled), then walk
//     the environment chunk list (occlusion-group table g_Environment+8 when a0>=0, flat count-list
//     g_Environment+0/4 when a0<0), culling each chunk by view frustum + distance (MVMVA against
//     the view matrix, radius shift thresholds vs m_LodDistance) and applying the per-chunk
//     environment geometry/colour animation (INTPL/DPCS blocks writing into chunk vertex/colour
//     arrays). Survivors are written to the work list at D_8006FCF4+0x1C00.
//   Phase 2 (0x800261A0..0x8002A0B0): project every surviving chunk's vertices (RTPS), write
//     SXY2+SZ3 into the scratchpad vertex cache (0x1F800000, 8-byte interleaved), and emit gouraud
//     quads (0x24) and tris (0x1C) into the packet pool with NCLIP backface culling, depth/LOD
//     test, whole-face clip rejection, and OT binning by average SZ. This is where native DEPTH is
//     emitted.
//   Phase 3 (0x8002A51C): emit the special-surface (water/lava) textured quads from D_8006D5C8.
//   Epilogue: publish the pool pointer to D_800757B0, link g_WorldOT, restore registers, return.
//
// BYTE-EXACT IS THE ADMISSION REQUIREMENT, not a stretch goal. ndiff snapshots RAM, the scratchpad,
// every GPR and the COP2 register file, runs this body, rewinds, runs the recompiled body, and
// compares. An identity probe proved the differential CAN validate a function of this shape (C202,
// 20/20 exact) and the depth oracle measured the ground's real SZ3 range + per-face OT bins (C201).
//
// The transcription is a faithful render of the generated body (generated/shard_3.c), which is the
// byte-exact reference: same GTE ops, same memory writes, same register effects. It is written with
// named constants and named registers so the clip bounds, the pool, the OT and the GTE plumbing are
// auditable, and so the depth hooks can be added at the projection sites.
#include "cfg.h"
#include "core.h"
#include "game.h"
#include "native_diff.h"
#include "rec_decls.h"
#include "recomp_iface.h"
#include "spyro_game.h"
#include <lucent/log.h>

namespace {

// ── Guest globals this renderer reads and writes (C198 / whatis), named rather than raw.
constexpr uint32_t kSaveArea = 0x80077DD8u; // fixed register-save block (no stack frame)
constexpr uint32_t kCamera = 0x80076DD0u;   // g_Camera: view matrix +0x14, position +0x28
constexpr uint32_t kEnvironment = 0x800785A8u; // g_Environment: +0 m_SectorPointer, +4 count,
                                               // +8 occlusion groups, +0x18 cull list, +0x24 lod
constexpr uint32_t kWorkList = 0x8006FCF4u; // + 0x1C00: the cull-phase survivor chunk list
constexpr uint32_t kPoolPtr = 0x800757B0u;  // packet-pool write pointer
constexpr uint32_t kOtBase = 0x80075820u;   // g_WorldOT (the ordering table base)
constexpr uint32_t kScratchpad = 0x1F800000u; // the vertex cache (SXY2 + SZ3, 8-byte stride)
constexpr uint32_t kProducerKey = 0x800258F0u;

// GTE opcodes used (from the recompiled body's gte_op_at arguments).
constexpr uint32_t kGteMvmva = 0x4A49E012u; // MVMVA, sf=1, v=IR, l=0
constexpr uint32_t kGteIntpl = 0x4A980011u; // INTPL
constexpr uint32_t kGteDpcs = 0x00000000u;  // (Phase 1 uses INTPL + DPCS; opcodes from generated C)

} // namespace

// The native body. Transcribed from generated/shard_3.c gen_func_800258F0 to stay byte-exact.
// WORK IN PROGRESS: this is the prologue + Phase 1 (the cull + animate pass, 0x800258F0..0x800261A0).
void world_native(Core *c) {
  uint32_t &s0 = c->r[16], &s1 = c->r[17], &s2 = c->r[18], &s3 = c->r[19];
  uint32_t &s4 = c->r[20], &s5 = c->r[21], &s6 = c->r[22], &s7 = c->r[23];
  uint32_t &t8 = c->r[24], &t9 = c->r[25], &gp = c->r[28], &sp = c->r[29], &fp = c->r[30];
  uint32_t &ra = c->r[31];

  // Phase 1 prologue: save callee-saved registers to the fixed area (no stack frame).
  c->mem_w32(kSaveArea + 0u, s0);
  c->mem_w32(kSaveArea + 4u, s1);
  c->mem_w32(kSaveArea + 8u, s2);
  c->mem_w32(kSaveArea + 12u, s3);
  c->mem_w32(kSaveArea + 16u, s4);
  c->mem_w32(kSaveArea + 20u, s5);
  c->mem_w32(kSaveArea + 24u, s6);
  c->mem_w32(kSaveArea + 28u, s7);
  c->mem_w32(kSaveArea + 32u, gp);
  c->mem_w32(kSaveArea + 36u, sp);
  c->mem_w32(kSaveArea + 40u, fp);
  c->mem_w32(kSaveArea + 44u, ra);
  ra = c->r[4]; // a0 = occlusion group

  // Load the camera view matrix into the GTE (g_Camera+0x14..0x24), translation nulled.
  uint32_t t2 = c->mem_r32(kCamera + 0x14u);
  uint32_t t3 = c->mem_r32(kCamera + 0x18u);
  uint32_t t4 = c->mem_r32(kCamera + 0x1Cu);
  uint32_t t5 = c->mem_r32(kCamera + 0x20u);
  uint32_t t6 = c->mem_r32(kCamera + 0x24u);
  gte_write_ctrl(0, t2);
  gte_write_ctrl(1, t3);
  gte_write_ctrl(2, t4);
  gte_write_ctrl(3, t5);
  gte_write_ctrl(4, t6);
  gte_write_ctrl(5, 0);
  gte_write_ctrl(6, 0);
  gte_write_ctrl(7, 0);

  // Camera position (g_Camera+0x28..0x30), each >> 4.
  s7 = c->mem_r32(kCamera + 0x28u);
  t8 = c->mem_r32(kCamera + 0x2Cu);
  t9 = c->mem_r32(kCamera + 0x30u);
  s7 = (uint32_t)((int32_t)s7 >> 4);
  t8 = (uint32_t)((int32_t)t8 >> 4);
  t9 = (uint32_t)((int32_t)t9 >> 4);

  // Zero the work-list scratch region (D_800771C8..D_800771C8+0x100, 16 bytes per entry).
  s4 = 0x800771C8u;
  uint32_t at = s4;
  uint32_t v0 = s4 + 0x100u;
  do {
    at += 0x10u;
    c->mem_w32(at - 0x10u, 0);
    c->mem_w32(at - 0xCu, 0);
    c->mem_w32(at - 0x8u, 0);
    c->mem_w32(at - 0x4u, 0);
  } while (at != v0);

  // ── Phase 1 continues: the chunk-list walk (cull + animate). Transcribed in the next step. ──
}

// The owned override: dispatch the native body, and under ndiff verify it against the recompiled body.
void world_owned(Core *c) {
  ndiff_run(c, "world@0x800258F0", world_native, gen_func_800258F0);
}
