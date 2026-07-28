---
id: C083
kind: claim
status: holds
created: 2026-07-29
tags: perf,diagnostics
reconfirmed: 2026-07-29
---

## Claim

About 10% of this port's CPU time is DIAGNOSTICS overhead, most of it the logger's per-call channel check.

## Evidence

From the same 49516-sample profile: lucent::detail::channel_enabled(string_view) 6.76%, Core::wwatch_check 1.79%, cfg_dbg_generation 1.62% — 10.2% combined. wwatch_check runs even with no watchpoint armed, and channel_enabled does a string_view lookup per evaluated log site rather than a cached predicate. The run had one debug channel enabled (prof), which is a normal configuration, not a stress case.

## What would falsify it

a run with PSXPORT_DEBUG entirely unset would show whether channel_enabled is still hot when no channel is on — if it drops to near zero, the cost is per-enabled-channel rather than unconditional

## Re-confirmed 2026-07-29

FALSIFIER TESTED AND IT DID NOT FALSIFY — it strengthened the claim. Re-profiled with PSXPORT_DEBUG entirely unset (39556 samples): lucent::detail::channel_enabled is STILL 6.06%, versus 6.76% with one channel on. So the cost is UNCONDITIONAL, not per-enabled-channel: the logger burns ~6% of this port's CPU when logging is completely off. That makes it a framework performance bug rather than a configuration choice — the disabled path should be a cached predicate, not a string_view lookup per evaluated log site. Guest code measured 4.9% in this run, consistent with 4.5% in the first.
