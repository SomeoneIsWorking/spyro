---
id: C049
kind: claim
status: holds
created: 2026-07-28
tags: recomp,framework
---

## Claim

The remaining fail-fast is a COMPUTED-OFFSET jump idiom the recompiler does not model: target = base + (idx << k), base materialised by lui/addiu

## Evidence

Fully decoded at the failing site. 0x8004C4D4-0x8004C4D8 is lui+addiu materialising s2 = 0x8004C4EC; 0x8004C4B0-0x8004C4B4 is 'addi s1,zero,2 ; sll s1,s1,4' giving s1 = idx << 4; 0x8004C4E0 is 'add s2,s2,s1'; 0x8004C4E4 is 'jr s2'. So the target is base + (idx*16), a jump INTO AN UNROLLED RUN of 16-byte blocks starting at 0x8004C4EC — matching the observed case spacing (0x8004C4EC, 0x8004C4FC). Two further runs exist at 0x8004C550/0x8004C560 and 0x8004C5D0/0x8004C5E0, each with its own base and converging continuation. emit.py's find_jump_tables cannot see this BY DESIGN: both idioms it recognises (A and B) require the target ADDRESS to be read from a table with 'lw rN,OFF(base)'. Here there is no table and no lw — the base is an immediate and the index is scaled into an offset. That is why 121 case labels were pruned for OVL1 (a real table) while this one routes to rec_dispatch and fail-fasts. The base being an lui/addiu immediate means it IS statically recoverable, so this is a recogniser gap rather than an undecidable case.

## What would falsify it

Finding an lw-based table that actually feeds this jr, which would mean the strict recogniser should have matched and the fault is elsewhere.
