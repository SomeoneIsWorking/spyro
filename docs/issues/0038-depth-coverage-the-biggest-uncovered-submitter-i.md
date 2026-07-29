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
