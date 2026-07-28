---
id: 17
title: recomp-MISS at 0x8008772C — a call into memory no load ever wrote
status: resolved
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

### Resolution (2026-07-28)
ROOT CAUSE IDENTIFIED (C039). Not a missing seed, and not a function-discovery gap.

0x8008772C is one of 43 HANDLER POINTERS the installer at 0x8005A4BC-0x8005A694 writes into the global
[0x800758CC]; the guest loads that global and jalr's it at 0x80014478 with a0=0x78. Seven of the 43
point inside OVL0; the other 36 span 0x80080548-0x8008B2C0, which is 67720 bytes above the arena base
0x8007AA38 and therefore consistent with a single LEVEL overlay loaded at that same base (level overlays
measure 38912-81920 bytes, C033). 36 also matches the level-overlay count.

So the game selected a level, installed that level's handler table, and then CALLED a handler without the
overlay ever being loaded — only six loader calls happen in the whole run and OVL0 is the only code
overlay among them.

The bug is therefore the MISSING LOAD, not the missing recompilation. Do not seed 0x8008772C: at the
moment of the call nothing has been written there, so a seed would recompile whatever happens to be in
that memory.

NEXT: find what is supposed to trigger the level overlay load between the handler install and the call.
The two are close together in the same flow, so the trigger is likely a state check that is failing —
note 0x80014464 tests [0x80075690] and SKIPS the call when it is nonzero, so that global is worth reading
first.
