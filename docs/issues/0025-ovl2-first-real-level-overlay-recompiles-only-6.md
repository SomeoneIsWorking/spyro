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
