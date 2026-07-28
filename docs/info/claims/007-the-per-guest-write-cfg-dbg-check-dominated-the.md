---
id: C007
kind: claim
status: holds
created: 2026-07-28
tags: perf
---

## Claim

The per-guest-write cfg_dbg check dominated the substrate profile; frame count is the WRONG metric for it

## Evidence

6-sample stack profile of the running Spyro port: 6/6 samples in lucent::detail::channel_enabled <- cfg_dbg <- OtAttr::trackStore <- pkt_track <- Core::mem_w32. After caching the answer behind a generation counter: 0/4 samples there, profile moved to guest code. Frame count was UNCHANGED (8 in 30s both before and after) because the guest presents its splash then enters a long non-presenting phase — frame count measures guest progress, not CPU throughput. Fixed upstream in psxport.

## What would falsify it

if a later profile shows channel_enabled back on the hot path, the cache was invalidated too often or a new uncached cfg_dbg call site was added to a per-write path
