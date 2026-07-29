---
id: 37
title: Widescreen needs the terrain renderer owned: its clip bounds are immediates, so OFX cannot be shifted independently
status: open
symptom: Widening the view requires shifting the projection centre (OFX) into a wider framebuffer, but the guest's own trivial-reject test compares the PROJECTED sx against hardcoded bounds 0 and 512<<16. Shift OFX alone and previously-visible geometry starts failing the right-hand test — the picture gets worse, not wider.
tags: gpu,widescreen,ownership,re
created: 2026-07-29
updated: 2026-07-29
---

MEASURED, so the opportunity is real: the GTE already projects ~25% of vertices outside the 512-wide frame (C127). The content exists; the game throws it away.

WHERE IT IS THROWN AWAY. 0x8004ED84-8C loads three immediate bounds into t5/t6/t7 (1<<16, 256<<16, 512<<16). 0x8004EE0C-EE3C turns them into a 4-bit clip code per vertex — sy<=0, sy>=256, sx<=0, sx>=512 — packed into the low bits of the cached vertex word. Stage 2 ANDs the three vertices' codes and skips the face when they share an off-screen side (Sutherland-Cohen trivial reject).

WHY THIS FORCES OWNERSHIP RATHER THAN A TWEAK. The bounds and the projection centre have to move TOGETHER. They cannot: OFX lives in a GTE control register the port can influence, but the bounds are immediates inside recompiled guest code. Patching guest code is not on the table (the substrate is sacrosanct, and a magic constant edit is exactly the bandaid class the project rules ban). So the honest route is to OWN this renderer natively — reimplement stage 1 (unpack, project, clip-code) and stage 2 (face walk, cull, packet emit) in C, where the bounds and the centre are ours.

THAT IS ALSO THE RIGHT DIRECTION INDEPENDENTLY: it is the 'own handlers top-down' step of the porting guide and the user's standing 'more PC driven' directive, and native depth (C125) already gives the per-vertex Z such a renderer needs.

PREREQUISITE ALREADY MET: the renderer is fully understood at instruction level — vertex format is 11/11/10-bit packed deltas, the scratchpad cache is indexed by pre-scaled byte offsets from the face list, packets are POLY_FT3 (stride 0x1C) and F3 (0x14). See issue 0034/0036 notes and the disassembly at 0x8004EBA8-0x8004EF64.

SCOPE WARNING, stated up front: this is a byte-exact reimplementation of a hand-written assembly renderer. It must be gated by the per-call differential (ndiff) against the recompiled body BEFORE any widening is switched on, or a rendering difference will be indistinguishable from a widescreen artefact.

### Note (2026-07-29)
SCOPE MEASURED, and it is larger than 'own the terrain renderer' — plus a correction to my own depth claim.

THE ASSEMBLY-RENDERER FAMILY IS 19 FUNCTIONS, not one. Scanning for the fixed-area register-save prologue (lui at,0x8007 ; addiu at,at,0x7DD8 ; sw s0,0(at)) finds 19 entries: 0x8001F158 0x8001F798 0x800208FC 0x80020F34 0x80022A2C 0x80023AC4 0x800258F0 0x8004AE38 0x8004BE4C 0x8004D5EC 0x8004DF24 0x8004E3C8 0x8004EBA8 0x8004F000 0x8004F4BC 0x8004FEA0 0x80050240 0x800580F4 0x80058D64. At least 8 of them carry the clip-bound immediates (lui rX,0x0200 = 512<<16 and lui rX,0x0100 = 256<<16), so the 512-wide trivial reject is spread across the geometry family, not localized to 0x8004EBA8.

TOOL GAP FOUND AND WORTH FIXING: tools/writers.py reported 0 writers for 0x80077DD8 even though 19 functions store there every call. It only understands lui + store-with-immediate-offset; this idiom is lui + addiu + store-with-ZERO-offset, so the address is complete in the base register before the store. That blind spot returns 'nothing writes this', which is the worst kind of wrong answer (it reads as dead state). Fix it to resolve lui+addiu bases.

DEPTH COVERAGE IS THE REAL BLOCKER FOR BOTH ENHANCEMENTS. C125 has been falsified and replaced by C128: native depth is mechanically working but reaches only 2.5% of primitives (23281 of 921709 over a full run; 698 of 826 prim-bearing frames get none). I had sampled a frame the ndepth summary reported 3D prims for, which selected for success.

WHY THAT CHANGES THE PLAN. psxport's fps60 re-runs the field world natively under lerped inputs (fps60.cpp: 'tier1Render re-runs the field world into mSink'), and explicitly aborts-with-identity for a sub-scene with no native world producer. Spyro has no native world producer at all — it replays guest packets. So lerp-60 as the framework implements it needs the same ownership work widescreen does. Neither is a cheap follow-on to native depth.

THE CHEAPER ORDER, and it is a genuine prerequisite for both: raise DEPTH COVERAGE first. Every renderer whose vertices resolve is a renderer whose geometry can later be reprojected or interpolated, and coverage is measurable per frame with the ndepth summary, so progress is visible without owning anything. Start by identifying which renderers submit the ~1900 prims/frame in the 698 zero-depth frames (PSXPORT_WWATCH_BT on one of their packet addresses names the generated function, as it did for 0x8004EBA8), then extend the taps to those submit idioms.

DO NOT start a byte-exact reimplementation of 8-19 hand-written assembly renderers before that: it is a large, high-risk unit of work whose payoff is invisible until it is complete, whereas coverage improves measurably in small steps.
