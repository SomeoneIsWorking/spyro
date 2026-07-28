---
id: C089
kind: claim
status: holds
created: 2026-07-29
tags: perf,instrument
---

## Claim

Removing profile time STOPPED translating into throughput: round one (~11% of CPU) gave +12.6% frames, round two (~6%) gave none — 18809 frames, inside the 18537-18929 band it started from.

## Evidence

Round one (lucent channel_enabled 6.06% -> 0.33%, watch hooks ~4.9%): frames 16508 -> 18586, repeats 18537/18929. Round two (cfg_dbg_generation 3.42% + OtAttr::trackStore 2.54%, both verified gone from the profile against their own binary): frames 18809 — indistinguishable. Bytes from disc, CD completions and overlay count are IDENTICAL across both rounds (13178880 / 63 / 7), so the run reaches exactly the same point either way. LIKELY but UNCONFIRMED: with round one's overhead gone the workload is no longer bound by this code — 28.7% of samples sit outside the binary entirely (Vulkan driver / loader) and that share does not shrink. Not proven.

## What would falsify it

a CPU-bound workload — gameplay rather than the attract loop — could show round two's saving as real throughput; this is one workload
