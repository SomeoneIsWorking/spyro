---
id: C090
kind: claim
status: holds
created: 2026-07-29
tags: gpu,tooling,instrument
---

## Claim

The 'VK readback blocks headlessly' dead-end was wrong: it was a SIGSEGV on an optional GameHooks entry (renderFadeState) that five gpu_vk.cpp sites called unguarded. Fixed; headless frame capture now works.

## Evidence

Port exit status is 139 (SIGSEGV), not a hang — the earlier diagnosis inferred blocking from 'one frame in 25s and no files', which is what an early crash looks like if you never check rc. renderFadeState is nullptr in Spyro's hooks (game_hooks.cpp:81) and TWO sites in gpu_vk.cpp already null-checked it, establishing it as optional by design; five did not. A step-trace through readback_vram now shows enter -> targets ok -> cmd acquired -> submitted -> fence signalled on every call, so the readback never blocked. After routing all sites through a null-safe fade_state_of(), 'preseq 6' from the REPL writes six 512x240 PPMs and the port exits cleanly.

## What would falsify it

if a consumer that DOES implement renderFadeState also fails to capture headlessly, then the null hook was only one of several faults
