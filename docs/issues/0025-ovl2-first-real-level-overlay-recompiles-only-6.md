---
id: 25
title: OVL2 (first real level overlay) recompiles only 6 functions — run fail-fasts on 0x8007CFB4
status: open
symptom: no recompiled fn for 0x8007CFB4 — overlay-router says OVL2 is resident, so this is a function-discovery gap inside OVL2, not a wrong overlay
tags: recomp,overlay,blocker
created: 2026-07-28
updated: 2026-07-28
---

CONTEXT. Reachable only since input started working (C063): the game leaves attract and loads OVL2 (WAD.WAD +0x237D000, 51200 bytes, arena base 0x8007AA38).

WHAT DISCOVERY GETS. main: 247 seeds -> 668 functions. OVL2: 1 seed -> 6 functions, out of a ~12800-instruction module. Overlay modules get no pointer/prologue scan, only jal-following from whatever seeds the json names.

WHAT IS KNOWN ABOUT THE MISSING ADDRESS. 0x8007CFB4 is inside OVL2's span and does NOT appear as a stored word anywhere — 0 occurrences across main's text and all three overlay images — so it is computed at runtime. It is not prologue-shaped either: it starts 'lui at,0x8007 ; addu at,at,v0 ; lw v0,0x6378(at)', a table-indexed load, which is legal for a leaf but is also what a mid-function label looks like. The reported caller ra=0x80033AAC is STALE (issue #14); c->pc says the live frame is func_8002A6FC, a table-driven script VM walking [0x80078560].

WHAT IS ALREADY RULED OUT. It is not one of main's per-level entry pointers: 43 stores to [0x80075734] at 0x8005A4CC-0x8005B6BC resolve to 41 distinct entries (matching the decomps' ~37 overlays) and 0x8007CFB4 is not among them. No overlay writes [0x80075734] at all (full lui+simm scan of all three images).

WHY NOT JUST SEED THE 41. Every level overlay loads at the SAME arena base, so another level's entry address lands mid-function in OVL2 and splits a real function — the exact corruption recomp_seeds.json exists to prevent.

LIKELY RIGHT FIX. A prologue scan of each overlay IMAGE (jr ra + delay slot, then addiu sp,sp,-N), which is how callgraph.py and wad_index.py already recover function boundaries in this repo, applied per-overlay so only that module's own layout is used. 0x8007AEB8 was added as an OVL2 seed this way (clean prologue, reached via 'lw v0,[0x80075734] ; jalr v0' at 0x80033AA4) and bought exactly one more step — one miss per rebuild is not a workable loop.

### Note (2026-07-28)
REFRAMED — 'starved discovery' is probably the WRONG diagnosis. Measured over the raw overlay images (base 0x8007AA38):

  OVL0  3584 words  189 jal /  86 in-span (2 distinct)   5 jr-ra   header = ASCII 'BASCUS-94228SPYRO' (a memory-card title block)
  OVL1 16384 words  969 jal /   0 in-span               9 jr-ra
  OVL2 12800 words  783 jal /   0 in-span               5 jr-ra

Zero in-span jals and a handful of jr-ra across 64 KB is not code-with-missing-seeds — these overlays are mostly LEVEL DATA with a small amount of embedded code, and the hundreds of 'jal' opcodes are coincidental byte patterns in that data. OVL1 even contains level text ('...THE BALLOONIST...'). So 6 recompiled functions for OVL2 may be close to ALL the code it has, and the real question is what 0x8007CFB4 actually is.

DEAD END, tested and falsified: 'the overlay starts with a count + function-pointer table'. Both images do begin with a plausible count (OVL1 0x0C, OVL2 0x15) followed by in-span-looking addresses, which is seductive. But NONE of the 33 targets starts with a prologue (), and OVL1 entries 8-11 decode as ASCII ('s% T','HE B','ALLO','ONIS') — it is a data structure, not an export table. Do not seed from it; every entry would land mid-something and split real code.

WHAT 0x8007CFB4 LOOKS LIKE. Not a prologue: 'lui at,0x8007 ; addu at,at,v0 ; lw v0,0x6378(at)' — the shape of a switch-case body / jump-table tail, and emit.py reports pruning 113 jump-table case labels from OVL2. So the next thing to establish is whether it is reached by a CALL at all or by a computed  — i.e. whether this belongs in overlay_seeds (a function) or is a re-entry/case-label problem like issue 0020. c->pc at the fail-fast is func_8002A6FC, a table-driven script VM walking [0x80078560]; its dispatch is the place to read.
