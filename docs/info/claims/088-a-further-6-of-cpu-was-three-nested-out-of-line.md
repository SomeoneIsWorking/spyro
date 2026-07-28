---
id: C088
kind: claim
status: holds
created: 2026-07-29
tags: perf,diagnostics
---

## Claim

A further ~6% of CPU was three nested out-of-line calls per guest store deciding NOT to log: OtAttr::trackStore -> cfg_dbg_generation -> bootstrap_once.

## Evidence

Fresh profile against its own binary: cfg_dbg_generation 3.42%, OtAttr::trackStore 2.54%. trackStore runs on every guest store and reached the generation counter through two nested calls purely to compare it against a cached value. Fixed by exposing the counter as an extern read inline (cfg_dbg_generation_fast) and moving trackStore's armed test into ot_attr.h. Both vanish from the profile. VERIFIED IN BOTH DIRECTIONS: with PSXPORT_DEBUG=otattr the armed path is reached and costs 8.24%, so the gate does not wrongly block the feature it guards.

## What would falsify it

if trackStoreSlow ever appears in a profile with otattr OFF, the member cache is not sticking and the fast path has stopped engaging again
