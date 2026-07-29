---
id: C101
kind: claim
status: holds
created: 2026-07-29
tags: gate,perf,instrument
---

## Claim

The gate's own frame dump was costing the port ~2.8x its speed — 19003 frames per 40s run with every frame dumped, 54151 with the dump sampled.

## Evidence

Same 40s headless run, same build, only PSXPORT_GPU_DUMP changed from 'dir' to 'dir:20': frames presented 19003 -> 54151, dump volume 6.6 GB / 19003 files -> 963 MB / 2708 files. So the instrumentation was the dominant cost in the measured run, and every frames-presented figure recorded earlier in this session was taken on a port carrying that I/O.

## What would falsify it

if a sampled run on an idle machine shows the same ~19000, the difference was external load rather than the dump — the machine was busy with another project throughout (C092)
