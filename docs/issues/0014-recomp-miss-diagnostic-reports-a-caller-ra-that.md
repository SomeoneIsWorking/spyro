---
id: 14
title: recomp-MISS diagnostic reports a caller 'ra' that is not always a code address
status: dead-end
symptom: A [recomp-MISS] line printed 'caller ra=0x80078A58', which reads as code executing in an undiscovered region between text_end and the overlay arena.
tags: diagnostics,recomp
created: 2026-07-28
updated: 2026-07-28
---

0x80078A58 is a .bss GLOBAL, not code. Nothing in the resident text jumps or calls there, nothing in
OVL0 does either, and it is among the most heavily referenced addresses in the level overlays (51-73
lui/addiu references each) — the signature of a large engine struct base.

So the 'ra' shown alongside a recomp miss is whatever register 31 happened to hold, which is only a
return address when the miss arrived through a normal call. Recorded as a DEAD END so the next session
does not spend a cycle hunting a second overlay region at 0x80078xxx. The seed rationale in
game/recomp_seeds.json that quoted this line now carries the same warning inline, since that is where
someone will actually read it.

The seed itself (0x80024054, reached only via a function pointer) remains correct and justified — it
was a real miss at a real resident-text address. Only the ra annotation was misleading.
