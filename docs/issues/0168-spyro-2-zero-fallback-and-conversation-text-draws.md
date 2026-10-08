---
id: 168
title: Spyro 2 runs with zero fallback blocks, and its conversation text does draw (the early capture caught the box before its text)
status: resolved
symptom: Spyro 2 needed a 59-block diagnostic fallback allowance; its first conversation looked like dark boxes with no glyphs
tags: spyro2,lightrec,fallback,dialogue,text,level
created: 2026-10-01
updated: 2026-10-01
---

## Answer

Two defects were filed against Spyro 2 after issue 0167. One was real and is fixed at its cause in the shared
Lightrec fork; the other was not a defect in any text path.

### A. The 0x8005FFFC self-modifying-code false positive (real, fixed in `shared/lightrec`)

Mechanism, decoded from the PS-X EXE text (file offset 0x800) with `external/psxport/tools/disasm.py`
(44/44 words decoded) and read against Lightrec's source:

```
8005FFFC  lui  $at,0x8006         ; block entry: a conditional-branch target (8005FFB8 bne ... 8005FFFC)
80060000  addu $at,$at,$v0
...
80060028  lui  $at,0x8006         ; $at = 0x80060000, a known constant (constprop)
8006002C  sw   $v0,0x6c58($at)    ; the store's real address is 0x80066C58 (data)
...
80060050  jal  0x8005ef74         ; first UNCONDITIONAL jump: the block is [0x8005FFFC, 0x80060058)
```

`lightrec_flag_io` (optimizer.c) marked a block `BLOCK_NEVER_COMPILE` when a store's BASE register value
lay inside the block's own range. The base `0x80060000` does, and the 16-bit displacement `0x6C58` that moves
the store 27 KiB out of the block was discarded. A `lui`-built base is 64 KiB aligned, so any block starting
within 64 KiB after the `lui` constant has every store through that base refused.

Fix: `shared/lightrec` `4696481` "Compare a store's resolved address, not its base, for self-modifying code":
the question is asked of base plus the sign-extended displacement. Its contract test
`tests/smc_detection_test.c` builds 22 blocks; **the old rule fails it (15 data-store cases: 6 pass), the new rule
passes it (15 of 15; 7 genuinely self-modifying stores are still detected)** - re-run for this issue by rebuilding
the test against `20bc8a2`'s `optimizer.c` (red) and `4696481`'s (green). psxport `s2-lr` `41373bc0` pins it.

Measured on Spyro 2 (`tools/title_route.py --title spyro2`, no allowance, shipping default limit 0): gameplay
reached at field 3190, 0 faults, `fallback_blocks=0 fallback_instructions=0`, `refused_fallback_blocks=0`,
93,178,355 blocks and 662,007,092 instructions executed. Before: 59 fallback blocks, all `self_modifying_code`,
reachable only under `PSXPORT_LIGHTREC_FALLBACK_BLOCK_LIMIT=2000` (as recorded in issues 0092 section 4 and 0167;
the "before" run was not repeated here, there is no build of the earlier pair in this tree). The allowance
plumbing is gone: `TitleProfile.fallback_allowance`, `title_route.py --fallback-limit` and its DIAGNOSTIC line,
and `boot_run.py --fallback-limit` with `boot_log.render`'s raised-budget line.

### B. Spyro 2's conversation text (not a missing text path)

Walking forward from the arrival point (`tools/title_conversation.py --title spyro2`, `Up`, the guest state
`0x800681C8` becoming 1 after 50 fields) draws the whole speech through the guest's own path, with no native
producer involved: "Hi Spyro, welcome to Glimmer! Unfortunately for us, a mob of lizards just showed up and
started stealing all of our gems. Can you stop them?" under the speaker strip "Pogo the Gemcutter". The guest
reveals the words one character at a time after the box opens, and the box shrinks away empty at the end, so a
capture taken in the opening window is a dark rounded rectangle with no glyphs, which is what 0167 recorded.

Evidence, 30 captures one per 30 fields, glyph-coloured pixels (the two font colours `(176,136,8)` and
`(192,144,8)`, exact match) in the box body, 34,854 pixels examined per capture: `talk00` 0 (no box yet),
`talk01` 0, `talk02` 0 (box open, empty), `talk03` 170, `talk10` 929, `talk20` 1945, `talk29` 2879. The control is
the same count over the settled gameplay frame before the walk: 0. Opened and read:
`scratch/play/spyro2-talk/talk{00,02,03,10,20,29}.ppm`.

Not reproduced: whether the early capture was also affected by the fallback allowance (the interpreter-run
blocks); no build of the earlier pair exists here. The captured counts say the text path is intact with zero fallback.

### Level identity (from guest words, not the picture)

Spyro 2's arrival level is **Glimmer**, level id 11. The guest's name pointer table at `0x800649B4` is indexed
`(homeworld << 5) + level` (`0x80014450..0x8001447C`: `lw 0x7118`, `sll 5`, `+ lw 0x6F54`, `sll 2`,
`lw 0x49B4($at)`); the homeworld word `0x80067118` = 0, the level word `0x80066F54` = 1, the id word
`0x80066F90` = 11 (read at `0x80053D54` and mapped through the byte table at `0x80064940`), and the pointer read
at run time is `0x80066EA0`, the string "Glimmer". `title_route.py` now reports it and its selftest refuses a
pointer that names no printable string. It lives in Summer Forest (homeworld 0).

## Gate

`tools/title_conversation.py --selftest` (accepts a revealed conversation; refuses: a box that never draws a
glyph, a walk that opens nothing, a box full on its first capture, a title with no observed conversation, a
truncated image) and `title_route.py --selftest` (adds a level read and a refused empty name).
