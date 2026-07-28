---
id: C044
kind: claim
status: holds
created: 2026-07-28
tags: overlay,recomp
---

## Claim

OVL0's intro state machine is FULLY recompiled — the 'skipped checkpoints' are not a recompilation artifact

## Evidence

Checked because a truncated function body would produce exactly the observed symptom (a state machine appearing to skip its guard-reset sites) while being a port bug rather than game behaviour. It is not truncated. The emitted body of ov_ovl0_gen_8007ABAC in generated/ov_ovl0_shard_1.c is 1821 lines with 168 branch labels spanning L_8007ABE8..L_8007CD14, which covers every site in the extracted transition table including the guard writes at 0x8007B0AC/0x8007B900/0x8007C7C8/0x8007C8A8/0x8007CA64 and the handoff at 0x8007CC20. NOTE ON METHOD: two obvious checks were USELESS and I nearly misread both. Searching the body for the site addresses as hex literals returns nothing, and searching for the state globals (0x80078D94 etc.) returns zero occurrences — because the emitter computes every address through register arithmetic, e.g. the body's first lines are 'r2 = 32776u << 16; r2 = mem_r32(r2 + -29320)' which is 0x80080000-0x7288 = 0x80078D78, the sub-state global. Absence of a literal proves nothing here; the branch-label range is the signal that works.

## What would falsify it

Finding a reachable OVL0 address outside L_8007ABE8..L_8007CD14 that the state machine needs, or a [recomp-MISS] inside the OVL0 range.
