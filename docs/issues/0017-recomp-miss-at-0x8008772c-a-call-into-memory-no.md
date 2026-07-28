---
id: 17
title: recomp-MISS at 0x8008772C — a call into memory no load ever wrote
status: open
symptom: [recomp-MISS 0] no recompiled fn for 0x8008772C (caller ra=0x80014480, a0=0x00000078, c->pc=0x80058B68). Reached only after the render-queue flush let the boot advance past frame 3781.
tags: recomp,overlay
created: 2026-07-28
updated: 2026-07-28
---

0x8008772C is above the arena base 0x8007AA38 and above OVL0's end (0x8007E238) — 0xCCF4 past the
arena base, which is within the size range of the level overlays (38912-81920 bytes), so the tempting
reading is 'a level overlay is resident and undiscovered'.

THAT READING IS WRONG, and checking cost one run. PSXPORT_DEBUG=cdq,ovload shows only SIX loader calls
for the whole run and none of them writes anywhere near 0x8008772C: the arena receives the index
sector, one sector from +0x5F000, then OVL0 (14336 bytes), and the bulk loads go to 0x801BF800 /
0x801A4800 / 0x8018B800. Nothing was ever loaded at 0x8008772C, so this is a call through a bad
function pointer into never-written memory, not a missing overlay.

Do NOT add it as a seed. A seed at an address no image occupies would recompile whatever garbage is
there. The question to answer first is where that pointer comes from — a0=0x78 looks like an index,
so a dispatch table whose entries were never filled is a reasonable place to start.
