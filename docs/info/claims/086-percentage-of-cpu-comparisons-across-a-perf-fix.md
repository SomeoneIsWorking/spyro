---
id: C086
kind: claim
status: holds
created: 2026-07-29
tags: perf,instrument
reconfirmed: 2026-07-29
---

## Claim

Percentage-of-CPU comparisons across a perf fix are self-rebasing and cannot show the size of a win — only end-to-end throughput can.

## Evidence

After removing ~4.9% of call overhead, cfg_dbg_generation's share ROSE from 1.62% to 3.45% without its cost changing at all: the denominator shrank. Every remaining entry inflates the same way, so reading 'X went up' as a regression, or summing removed percentages as total speedup, is wrong in both directions. The measure that is not self-rebasing is frames presented in a fixed wall-clock run (the gate's own number), and even that varies run to run, so a single pair is weak evidence.

## What would falsify it

if frames-per-fixed-run does NOT rise across these two fixes, the profile deltas were measuring something that does not translate to throughput at all

## Re-confirmed 2026-07-29

FALSIFIER TESTED AND PASSED. The end-to-end measure did rise: frames presented in the SAME 40s gate went 16508 (13 native bodies, before any perf work) -> 17178 (after the lucent channel_enabled fix) -> 18586 (after inlining the per-store watch hooks), i.e. +12.6% across the two fixes. So the profile deltas did translate to throughput, and the percentage-rebasing caveat is about how to READ a profile, not a reason to distrust these two fixes. It also had a FUNCTIONAL consequence, which is the stronger evidence: in the same wall-clock the port now loads 13178880 bytes instead of 11147264, delivers 63 CD completions instead of 56, and identifies SEVEN distinct overlays instead of six — it reaches further into the game per run.
