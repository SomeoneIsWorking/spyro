---
id: C092
kind: claim
status: holds
created: 2026-07-29
tags: perf,instrument
---

## Claim

Perf numbers from this session were taken on a machine shared with another project's build, so absolute timings are confounded — the frames-presented comparisons should be treated as indicative, not precise.

## Evidence

A run that had completed comfortably in ~40s of a 120s budget started timing out at 100s with no code change that could explain it. The cause was external: another project (gears1) running at 213% CPU with a load average of 5.84. That process is the user's own work and was left alone. The variance triple (18537 / 18586 / 18929) was tight enough to suggest consistent conditions AT THAT TIME, and the step changes in what the run reaches (bytes from disc, overlay count) are load-independent — but the percentage figures and frame counts are not, and load was never controlled or recorded.

## What would falsify it

re-running the same gate on an idle machine and getting the same 16508-vs-18586 spread would show load did not distort the comparison
