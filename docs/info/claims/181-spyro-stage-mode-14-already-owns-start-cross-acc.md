---
id: C181
kind: claim
status: holds
created: 2026-08-14
tags: 
depends: generated/shard_4.c#gen_func_800331AC, generated/shard_1.c#gen_func_8002D440
---

## Claim

Spyro stage mode 14 already owns Start/Cross acceleration and exits through its natural completion writer

## Evidence

Generated main body 0x800331AC reads held [0x80077380]&0x840 when [0x8007566C]==1 and, within its guard window, rewrites playback cursor to cursor/2+16. At cursor>=sample_count*2 it calls 0x8002D440, whose emitted body performs cleanup and writes mode 13/stage handoff. scratch/logs/skipmap-replay-play.log reaches mode14 at field1912 and naturally leaves at 2183 amid Start edges, then traverses loading and modes0/2.

## What would falsify it

A live mode14 run where the masked Start/Cross branch does not execute despite guards, where 0x8002D440 is not the natural terminal path, or where mode14 is shown to be a different non-demo scripted family.
