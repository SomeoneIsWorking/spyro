---
id: 36
title: Native depth: Spyro's MAIN submits vertices via mfc2->GPR->sw, which the generic swc2 tap cannot see
status: resolved
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

### Note (2026-07-29)
PROGRESS + TWO MEASURED GAPS. Both sides of the native-depth chain are now live; it is coverage and identity that remain, not plumbing.

WHAT WAS ACTUALLY BLOCKING IT — not the GTE tap at all. gpu_dma2_block never stamped s_gp0_src, so every GP0 word Spyro submits arrived at the renderer with no source address. gp0_exec keys native depth on that address, so is3d was structurally 0 no matter what the tap recorded. 11469 of this game's 11523 words per frame come through block DMA. Fixed upstream; the renderer now asks (miss went 0 -> 1724).

THE TELL I MISREAD, recorded so it is not misread again: 'lookups hit=0 miss=0' means the ADDRESS side is missing — the renderer never even asked. 'hit=0 miss=N' means it asked and the key did not match. Those are different failures and the counter distinguishes them; I spent a while treating the first as a depth-recording problem. A per-frame 'gp0words (N addressed, M anon)' counter now makes it visible without inference.

ALSO MISREAD: I read 'records=0' from , which returns the FIRST three ndepth lines — early boot frames with no 3D. The level frames showed records=224 all along. Fourth wrong-regime read in this project. When a counter is per-frame, grep the LAST lines or the frame you care about, never the first.

REFUTED, before acting on it: I suspected the tap and the lookup disagreed by the KSEG0 bit (tap records 0x80xxxxxx, DMA keys on madr & 0x1FFFFC). ProjPrim::setPz and ::lookupPz BOTH already mask with & 0x1FFFFC, so the keys are physical on both sides. Read the code instead of fixing the theory.

GAP 1 — COVERAGE. Only 10 of MAIN's 70 mfc2-of-DR12-15 sites pair with a store. Categorised: 29 refused at a COP2 op, 29 at a block boundary, 2 at a redefinition (sra). The 29 COP2 refusals are SOFTWARE PIPELINING — the game reads vertex N's XY, issues the transform for vertex N+1, then stores N. Refusing is currently correct (DR19 has moved on) but recoverable: snapshot SZ at the mfc2 into a per-GPR slot and consume it at the store, which turns those 29 into taps. That is the next step and it is testable the same way (a COP2 op between mfc2 and sw must then PAIR and use the held Z).

GAP 2 — IDENTITY. records=224/frame against miss=1724: even the vertices that ARE recorded are not the ones being drawn. Either the 10 tapped sites belong to a different subsystem than the level geometry, or the guest builds vertices in one buffer and DMAs a copy. Settle it by dumping a few recorded addresses next to the s_gp0_src values the renderer looks up, in the same frame — do NOT assume which.

ACCEPTANCE, unchanged: PSXPORT_DEBUG=ndepth showing hit > 0, and the is3d column in PSXPORT_PRIMDUMP=<odd frame> flipping for real prims.

### Note (2026-07-29)
IDENTITY GAP DIAGNOSED — the vertices are projected into one buffer and the GP0 packets are built in another.

Added `debug pzaddr`, which prints the addresses RECORDED and the addresses that MISSED within the SAME frame. That distinction is the whole point: records=N/hit=0/miss=M cannot tell 'the wrong vertices are recorded' from 'not enough vertices are recorded', and those want opposite fixes. Measured, one frame:

    RECORD  1A8678  1A88C8  1A88D0  1A8B08  1A8B10  1A8CF8  1A8D00   (+ scratchpad 1F8000xx)
    MISS    1AB764  1AB780  1AB79C  1AB7B8  1AB7D4  ...              (stride 0x1C = a 7-word POLY_FT3)

Same 0x1A region, ~12 KB apart, same frame. So the projected XY is written into a vertex buffer (and, for a large share, into the SCRATCHPAD at 0x1F8000xx), and the primitive packets that actually get DMA'd are assembled elsewhere with the XY copied in. Nothing is stale and nothing is mis-keyed — the depth is simply attached to the wrong copy of the value.

COVERAGE IS NO LONGER THE BINDING CONSTRAINT, though it did improve: holding the Z at the mfc2 instead of reading it at the store took tapped sites 10 -> 26 and recorded vertices per frame 224 -> 1120. Peak counters now records=1120 lookups hit=0 miss=1655. More coverage of the same buffers will not produce a single hit.

NEXT STEP — PROPAGATE ACROSS THE COPY. Same local dataflow shape that already works twice: for `lw rX, off(src)` followed by `sw rX, off2(dst)` with no redefinition of rX and no branch between, emit a propagation call that looks up a pz at the loaded address and, if present, records it at the stored address. This is what PGXP does in emulators and it is game-agnostic. Test it the same way — a pair with an intervening redefinition must NOT propagate.

RISK TO WATCH: propagation must not fabricate depth. Only propagate when the source address HAS a recorded pz; a miss must stay a miss, otherwise 2D elements that happen to copy words through the same registers acquire a world depth and sort into the 3D scene.

A SECOND CANDIDATE, if the copy turns out not to be lw/sw: the transfer may be a DMA or a block copy routine, in which case the propagation belongs at that routine rather than in the recompiler. Measure which before building either — do not assume lw/sw.

TWO CORRECTNESS BUGS FIXED ALONG THE WAY, both from this measurement: (a) ProjPrim keyed on & 0x1FFFFC, which aliases scratchpad 0x1F800018 onto RAM 0x00000018 — a staged vertex could have answered a lookup for an unrelated packet, i.e. a WRONG depth; (b) defines_reg treated a COP2 op as a GPR writer, which silently killed every software-pipelined pairing.

### Note (2026-07-29)
MECHANISM IDENTIFIED AT INSTRUCTION LEVEL (escalated to Fable, then verified against the disassembly myself).

0x8004EBA8 is not a wrapper — the full-GPR save to a FIXED area (0x80077DD8, no stack) is the signature of Spyro's hand-written assembly terrain renderer. It is TWO-STAGE with a scratchpad vertex cache:

  stage 1 (0x8004EDF8-0x8004EE44): unpack 11/11/10-bit packed vertex deltas, RTPS (software pipelined),
      mfc2 v0,DR14 ; sll a0,v0,5 ; up to four CONDITIONAL addi a0,a0,1|2|4|8 packing the vertex's CLIP
      CODE into the freed low bits ; sw a0,0(s7) -> the scratchpad cache, s7 += 4
  stage 2 (0x8004EE84-0x8004EF64): each face word carries three pre-scaled cache byte-offsets; lw the
      three slots, AND the clip codes, cull with bgtz, sra ,5 to unshift, sw into the packet at fp,
      fp += 0x1C (FT3) or 0x14 (F3)

The 0x1C stride is the same one measured independently in the MISS addresses, which is the static/dynamic cross-check.

FALSIFIED: my 'these are wrapper/trampoline functions' reading. The register-save-to-fixed-area idiom marks a LEAF assembly renderer, and the stores wwatch attributed to it were the real ones all along.

WHY EVERY EARLIER TAP MISSED IT: stage 1 stores a DERIVED register (sll + conditional addi) so a same-register scan loses it, and the clip-code addis sit behind conditional branches; stage 2's in-place sra reads as a redefinition and the cull branch as a block boundary. Both scans were single-basic-block and same-register. Fixed upstream by tracking the value through identity-preserving derivations, crossing conditional branches (guarded by 'no label with an inbound edge from outside the walk'), and not stopping at the first store. 26 -> 49 vertex stores tapped.

ALSO A LATENT WRONG-DEPTH BUG, now fixed: the copy tap re-evaluated the load's address expression at the STORE site, but stage 2's load clobbers its own base (add t6,t6,s4 ; lw t6,0(t6)) — so it would have read the loaded VALUE as a pointer and carried an unrelated word's depth. The source address is now captured AT the load.

ARCHITECTURE, settled: address-keyed depth is RIGHT for this engine and should stay. The engine's own dataflow is address-keyed (a scratchpad slot per vertex; face lists indexing slots by byte offset). A value-keyed attach ring would fail here even before the same-pixel ambiguity, because the cached value is (sxy<<5)|clipcode, not the SXY the packet holds.

STILL hit=0, and this is now the whole remaining question. The taps ARE emitted inside the renderer (gen_func_8004EBA8 contains 1 gte_record_pz and 10 gte_copy_pz) and records hold at ~925/frame, but pzaddr shows every record in the scratchpad at STRIDE 8, while stage 1 writes stride 4 (s7 += 4) — so the records are coming from some OTHER tapped site, and the terrain renderer's own stage-1 store is still not recording. NEXT: find out why that one store does not fire. Check whether gen_func_8004EBA8's [lo,hi) actually covers 0x8004EE44 (the emitted function is only 292 lines for a ~900-instruction routine, so it may be split), and confirm with a targeted count of records at stride-4 scratchpad addresses.

### Resolution (2026-07-29)
RESOLVED — native depth works. 210/210 prims in a sampled level frame carry real per-vertex view-space Z, zero unresolved lookups (C125).

THE LAST TWO LINKS, both found by measurement:

1. THE COPY SOURCE WAS CAPTURED ONE LINE TOO LATE. gte_hold_src was emitted AFTER the load, so for the terrain renderer's `lw t6,0(t6)` — a load that clobbers its own base — it recorded the loaded VALUE as an address. The exact wrong-depth bug the hold exists to prevent, reintroduced by emission order. The test now pins the ORDER, not just the presence; asserting a call exists is not asserting it is correct.

2. THE CACHE LIFETIME WAS ONE FRAME SHORT, and this was the last link. reset() cleared every depth at frame end, which assumes record and draw happen in the same frame. Spyro double-buffers its packet pool: it fills one pool while the DMA draws the other, so a vertex recorded in frame N is drawn in frame N+1.

   HOW IT WAS IDENTIFIED, because the reasoning generalises: 6568 addresses appeared as BOTH a record and a later MISS. An addressing fault cannot produce that — if the key were wrong the two sets would be disjoint. Only a lifetime fault can. That one number turned 'somewhere in a long chain' into 'the cache is cleared too early'.

   reset() now retires the oldest generation instead of clearing. TWO generations, not 'keep everything': the pool is reused, so an entry older than one buffer flip describes a vertex that no longer occupies that address, and serving it would be a wrong depth.

BEFORE / AFTER at the same sampled frames:
    records=1120  lookups hit=0    miss=1655   is3d 0/1609
    records=1386  lookups hit=670  miss=0      is3d 210/210

A MEASUREMENT TRAP THAT COST TIME HERE: the ndepth summary only prints every 60 frames, so a primdump on an arbitrary odd frame (46501) shows is3d=0 while sampled frames show 100%. Dump a frame the summary actually reports on. This is the fifth wrong-regime read in this project.

WHAT THIS UNLOCKS: widescreen and 60fps interpolation both ride on real depth — painter order is only correct for the camera the game pre-sorted for.
