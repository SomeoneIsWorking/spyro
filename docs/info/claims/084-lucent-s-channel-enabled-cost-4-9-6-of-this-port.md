---
id: C084
kind: claim
status: holds
created: 2026-07-29
tags: perf,diagnostics,lucent
---

## Claim

lucent's channel_enabled cost 4.9-6% of this port's CPU with logging OFF; a lock-free fast path for the empty-channel-set case takes it to 0.33%.

## Evidence

The disabled path took g_mutex and constructed a std::string from the string_view before it could answer 'no' — an unconditional cost at every debug() site. Fixed in lucent (the shared library, not worked around locally): an atomic g_any_enabled/g_loaded pair set under the mutex by every mutator and read without a lock, so an empty set answers false in two relaxed loads. Measured on the CURRENT binary: channel_enabled 0.33% (131 of 39614 samples), down from 6.06% measured on the pre-fix binary. lucent's own test suite passes. THE FIRST ATTEMPT MEASURED AS NO CHANGE AT ALL — load_channels_locked() early-returns when the channel list is empty, which is exactly the case the fast path existed for, so g_loaded stayed false and the lock-free path never engaged. Correct-looking code that did nothing, caught only by re-measuring.

## What would falsify it

a workload that enables channels heavily would not benefit — the fast path only covers the empty set; the per-call string construction is still there when any channel is on
