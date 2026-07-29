---
id: 38
title: Depth coverage: the biggest uncovered submitter is 0x800258F0, whose 9 vertex reads are multi-path
status: open
symptom: Native depth reaches 2.5% of primitives (C128). In the zero-depth frames, a write-watchpoint with backtrace on a packet vertex word names gen_func_800258F0 as the innermost writer for 744 of 997 hits — far ahead of 0x80024054 (135), 0x80016784 (46), 0x80058D64 (42), 0x80022A2C (22).
tags: gpu,depth,coverage,re
created: 2026-07-29
updated: 2026-07-29
---

0x800258F0 is the hottest guest function in the profile (C082, 1.74% of CPU) and a member of the hand-written assembly renderer family (fixed-area GPR save to 0x80077DD8). It spans 0x800258F0-0x8002A6FC — 4995 instructions — with NINE mfc2 DR14 (screen XY) sites and EIGHT mfc2 DR19 (paired SZ) sites. Only 3 of the 9 currently produce a depth record; the emitted function contains 3 gte_hold_pz / 3 gte_record_pz alongside 93 gte_hold_src / 98 gte_copy_pz.

IT IS MULTI-PATH, which is why a single idiom does not cover it. At 0x800262F4:

    800262f4  mfc2 v0, DR14          ; projected screen XY
    800262f8  mfc2 v1, DR19          ; the paired view-Z, read into a GPR
    800262fc  bgtz s1, 0x8002631c    ; splits
    80026300  addi t7,t7,4           ;   (delay slot)
    80026304  sw v0, 0(sp)           ; path A: XY to the STACK, with the Z beside it
    80026308  sw v1, 4(sp)
    8002631c  sll a0, v0, 5          ; path B: the clip-code idiom, as in 0x8004EBA8 stage 1

Path B is the shape already solved for the terrain renderer. Path A stages through the STACK and its consumer is not yet identified — a depth recorded at a stack address is harmless but useless unless the later copy into a packet is followed.

NOTE THE Z IS READ INTO A GPR HERE (mfc2 DR19), unlike the terrain renderer where the tap reads the Z FIFO directly. If a path stores that GPR-held Z rather than the FIFO value, a tap keyed on the FIFO may take the wrong vertex's depth once the pipeline advances — check before extending.

WHY THIS ORDER: coverage is measurable per frame (the ndepth summary prints real-depth vs OT-band prims), so it improves in visible steps, unlike the byte-exact reimplementation of 8-19 assembly renderers that widescreen ultimately needs (issue 0037). Every renderer whose vertices resolve is one whose geometry can later be reprojected or interpolated.

A full per-site trace of all 9 sites was delegated; results to be folded in here.

### Note (2026-07-29)
PER-SITE TRACE DONE, THREE EMITTER FIXES LANDED, AND COVERAGE DID NOT MOVE. Recording the negative result plainly because it changes the strategy.

THE TRACE (delegated, then the key claims checked against the disassembly). All NINE mfc2 DR14 sites in 0x800258F0 reach a real GP0 packet — none is culling math, so there is nothing here to deliberately skip. It is a 6-block sequential draw pipeline over object lists, no swc2 anywhere. Sites 1-3 stage through the scratchpad and their stage-2 copy was killed by an unconditional forward j; sites 4-5 by a seed redefinition and an inbound-edge guard; sites 6-9 by the vertex store sitting in a BRANCH DELAY SLOT, with their stage-2 copies already tapped — one instruction per block breaking an otherwise complete chain.

FIXED IN psxport 0c85bb6a, all test-first: delay-slot stores are now tapped (spliced into the delay-slot statement, which is exactly where the hardware runs them); an unconditional forward j inside the function no longer stops the walk; and start-4 counts as inside the inbound-edge guard for a seed that is itself a delay slot. Tapped vertex stores in MAIN: 49 -> 74, none skipped.

THE RESULT: primitive coverage is 2.5% before AND after. Frames at 100% went 47 -> 55 and partial 81 -> 97 over a longer run, so more vertices genuinely resolve — but is3d requires EVERY vertex of a primitive to resolve, so partial gains are invisible in that metric. Across a run the lookup hit rate is 7.5% (87988 hit / 1078903 miss) against 4.69 MILLION records.

THAT RATIO IS THE FINDING. Recording is not the constraint — the port records 4.7M vertex depths per run and 92.5% of lookups still miss. The depths are being attached to addresses nothing draws from, because these renderers stage vertices through scratchpad caches and work arrays and then assemble packets from them through multi-hop paths that a bounded per-block scan cannot follow in general. Each fix follows one more hop; the engine has more hops.

STRATEGIC CONCLUSION, and it matches what the porting guide says independently: incremental tapping has hit diminishing returns for this game. Two fixes moved coverage from 0% to 2.5% (the address stamp and the cache lifetime, both structural); three further fixes moved it not at all. The honest path to broad depth — and therefore to widescreen and 60fps, which both need it — is OWNING these renderers rather than observing them, which is issue 0037's conclusion arrived at from the other direction.

DO NOT keep adding tap rules hoping for a threshold effect. If tapping is continued at all, the next measurement should be per-VERTEX resolution rather than per-primitive is3d, because that is what these fixes actually improve and the current metric cannot see it.
