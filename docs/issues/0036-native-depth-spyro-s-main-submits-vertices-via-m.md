---
id: 36
title: Native depth: Spyro's MAIN submits vertices via mfc2->GPR->sw, which the generic swc2 tap cannot see
status: open
symptom: After adding psxport's generic native-depth tap on swc2 of the projected screen-XY registers, projprim records=0 lookups hit=0 miss=0 in the attract demo, and all 1609 polys at f46501 still classify is3d=0. No world geometry gets real depth.
tags: gpu,depth,gte,re
created: 2026-07-29
updated: 2026-07-29
---

THE TAP IS NOT BROKEN — it is not reached. Measured instruction counts:

  MAIN (SCUS_942.28):  swc2 of DR12-15 = 0.  mfc2 of DR12 x7, DR13 x7, DR14 x56; mfc2 of SZ DR19 x40.
  OV_20F800.BIN:       swc2 of DR12-15 = 106.
  OV_18F800.BIN:       swc2 = 5, mfc2 = 5.
  generated/:          2753 gte_store_xy call sites emitted, all in the two overlay modules.

So the swc2 idiom EXISTS in this game but only inside OV_20F800, which is not resident during the attract demo (7 overlays are identified in a gate run; this is not one of them). Everything rendering in that scene reads screen-XY with mfc2 into a GPR and stores it with a plain sw, and by the time the sw executes the association with the GTE slot is gone.

WHY THIS MATTERS BEYOND SPYRO: 'gte_stsxy* compiles to swc2' is true of the SDK macro but NOT of hand-written or differently-compiled submit code, so a swc2-only tap will silently record nothing for some games. Silently is the problem — records=0 looks identical to a game with no 3D.

THE FIX, and it should stay generic: track the mfc2 -> sw path.  makes rX hold a projected screen-XY; a later  in the same basic block, with no intervening redefinition of rX, is the packet-vertex store and its address is the key. That is a small local dataflow analysis in the recompiler (emit.py), testable the same way the swc2 tap was: assert the tap is emitted for the mfc2/sw pair and NOT when rX is redefined in between. Prefer the static form over runtime GPR tainting — it costs nothing at run time and cannot drift.

ALSO NEEDED, and unverified so far: the SZ pairing. In the mfc2 form the guest reads SZ separately (DR19 x40 in MAIN), so the depth for a vertex must come from the GTE Z FIFO at the time of the mfc2, not at the time of the sw. Snapshot it at the mfc2.

DO NOT claim native depth until 'projprim(vtx) records=' is non-zero AND is3d flips for real prims — PSXPORT_DEBUG=ndepth prints that counter every frame, and PSXPORT_PRIMDUMP=<odd frame> gives the is3d column. Both are already wired.
