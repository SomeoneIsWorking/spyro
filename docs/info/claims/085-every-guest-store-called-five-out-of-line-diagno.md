---
id: C085
kind: claim
status: holds
created: 2026-07-29
tags: perf,diagnostics
---

## Claim

Every guest store called five out-of-line diagnostic hooks unconditionally; inlining the disabled test for two of them removed ~4.9% of CPU that was pure call overhead.

## Evidence

Core::mem_w8/16/32 each call display_pass_write_guard, wwatch_check, cw_check, pkt_track and journal_track before doing the store. Profiling with NO watch armed put cw_check at 3.14% and wwatch_check at 1.79% of total CPU — almost none of it the range test, all of it the call to reach a function that immediately returns. Moving the 'is anything armed' test inline (core.h) and leaving only the armed case out-of-line removes both from the profile ENTIRELY; some cost reappears in the inlined callers (mem_w32 4.17% -> 5.31%), which is the honest accounting. The armed path is unaffected and was VERIFIED still working: PSXPORT_WWATCH over a 25s run still reports 18 hits, so the fast path did not silently disable the feature.

## What would falsify it

if a run with a watch ARMED shows the same cost as before, the inline test is not actually being folded away and the win is illusory
