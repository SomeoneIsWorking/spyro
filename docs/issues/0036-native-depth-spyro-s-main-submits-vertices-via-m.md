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
