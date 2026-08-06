---
id: C163
kind: claim
status: holds
created: 2026-08-06
tags: memcard,irq,hostturn,stack
depends: game/core/vsync.cpp#run_vblank_callback
---

## Claim

A PSX guest's $sp is NOT a stack pointer at an arbitrary instruction, so any port that runs a guest interrupt handler asynchronously must switch to its own handler stack — Spyro's RenderWorldChunks keeps scratch values in sp/gp/fp

## Evidence

From the recompiled instructions, not inference: generated/shard_3.c gen_func_800258F0 saves sp/gp/fp in its prologue, then writes c->r[29]=-1, c->r[29]=0x1F800000 (scratchpad base) and c->r[29]=r1+7680 in its inner loops, and reloads all three from its frame at the end (5 sites for r29, 2 for r28). MEASURED consequence: with rec_host_turn_register armed and the vblank root handler run on c->r[29], the port died in RenderWorldChunks on an UNMAPPED read8 at 0x9006E9AB ~0.5s into a headless run (scratch/mcfix/logs/hostturn.log, reproduced with and without pad input). PSXPORT_DEBUG=hostturn located it: turns 25-31 at libetc function entries had sp=0x801FFFxx, turn 32 at a loop back-edge inside the renderer had sp=0x80071B00 gp=0x80071D20 — mid-table addresses (scratch/mcfix/logs/ht_where.log). Controlled A/B in one tree: host turn WITHOUT the guest callback dispatch ran 17151 turns over 4.5 min with no fault (scratch/mcfix/logs/e1.log); with it, the fault every time. Fixed by running the handler on a dedicated stack in the BIOS-reserved kernel region; measured peak usage 120 bytes of 8192.

## What would falsify it

a guest interrupt handler observed corrupting memory even with the stack switch in place, or gen_func_800258F0 shown to restore sp before any call it makes
