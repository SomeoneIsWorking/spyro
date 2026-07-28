---
id: C082
kind: claim
status: holds
created: 2026-07-29
tags: ownership,perf,instrument
---

## Claim

Guest code is only 4.5% of this port's CPU time — 95.5% is the port's own runtime. So native ownership of guest functions buys CORRECTNESS and architecture, not speed, and the static caller count I used to pick 15 targets is a poor proxy for hotness.

## Evidence

Host-PC sampling (PSXPORT_PROF=1, 1kHz, 49516 samples over a 50s headless run) resolved to symbols with nm. Guest recompiled code totals 4.5%; the top guest function is 0x800258F0 at 1.74%. Non-guest 95.5%, led by 35.9% outside the binary entirely (shared libs / Vulkan driver / loader), GpuState::gp0_exec 5.5%, GTE_Instruction 3.7%, Core::mem_w32 3.6%, GpuVkState::tex_emit 3.5%, MultiplyMatrixByVector 3.1%, Core::cw_check 3.1%, Core::mem_r32 2.8%. DECISIVE ON TARGET SELECTION: 0x800258F0 — the hottest guest function — has just TWO static callers, so own_candidates.py's ranking (which put 136-caller functions first) would never have surfaced it. None of the 15 owned bodies appears in the profile at all.

## What would falsify it

this is one workload — the attract loop. A profile taken in gameplay could shift the balance toward guest code; re-measure before generalising to the whole game.
