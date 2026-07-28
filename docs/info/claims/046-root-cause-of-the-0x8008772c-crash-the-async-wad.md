---
id: C046
kind: claim
status: holds
created: 2026-07-28
tags: 
---

## Claim

Root cause of the 0x8008772C crash: the ASYNC WAD read path (0x80016698) gets dataless synthesized completions, so the attract-demo level load 'completes' with zero bytes in the arena

## Evidence

0x80016698(a0=baseLBA,a1=dest,a2=len,a3=byteOffset,+arg5) is the game's async read: CdControl(Setmode 0x80)+CdIntToPos+CdControl(Setloc)+0x8006606c, sets in-flight flag [0x80076BB8]=1; 19 resident call sites (0x80014608..0x80015BC0) — it is THE level/streaming read primitive; 0x80016500 is only the sync boot loader. The mode-13/substate-3 path 0x80032B08 pumps 0x80015370 (14-phase jump table at 0x80010A88, gate: [0x80076BB8]==0 && 0x80063bd8(1,0)==2 && [0x800774B4]&0x40); its phase handlers call 0x80016698 with dest=arena base/cursor and per-level WAD offsets from the table at 0x8007A718 (guard==2 selects level from demo table 0x8006EE7C = {11,24,55,33,...} = attract demo levels; guard==2 deliberately skips the arena reset so OVL0 stays resident under the demo level). cd_queue.cpp's probe_80065DBC sets cd_completion_pending on EVERY read issue and cd_retry_step (0x800163E4 = the pump's first call each tick) delivers callback 0x80016490(a0=2) which just clears [0x80076BB8] — no data path exists for reads not routed through the 0x80016500 override. Run scratch/logs/fable_probe.log: after the 6 loader-served reads, 'read issued: dest=0x8007DDE8 lba=484' etc with NO loader: line, each followed by 'delivered CD completion', ending in recomp-MISS 0x8008772C (ra=0x80014480, a0=0x78).

## What would falsify it

0x80016698 gains a data-serving native override (or the cdc model transfers ReadN sectors); falsified if serving those reads does not remove the 0x8008772C miss
