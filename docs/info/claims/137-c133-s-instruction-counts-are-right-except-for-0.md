---
id: C137
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

C133's instruction counts are right except for 0x80022A2C, which is ~1058 instructions and not 598: its disassembly contains no jr within the first 700 instructions and the recompiled body runs to ~0x80023AB4. The remaining ownership queue is ~9150 instructions, not 8735.

## Evidence

Body extents read from the recompiled substrate (generated/shard_*.c, gen_func_<addr> definition to the next one, last L_ label + epilogue) rather than from the lui-scan classifier: 0x80022A2C ends ~0x80023AB4 (~1058), 0x8004F000 ~0x8004F4B0 (~300), 0x8001F798 ~0x800208E0 (~1094), 0x80020F34 ~0x80022A10 (~1719), 0x800258F0 ~0x8002A6E0 (~4988). Four of five agree with C133 within ~2%.

## What would falsify it

a body whose recompiled extent disagrees with a direct disassembly to its jr ra — the extent method trusts the recompiler's own function boundary
