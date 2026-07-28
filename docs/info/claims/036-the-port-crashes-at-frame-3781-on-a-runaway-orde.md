---
id: C036
kind: claim
status: falsified
created: 2026-07-28
tags: gpu,blocker
falsified_on: 2026-07-28
---

## Claim

The port CRASHES at frame 3781 on a runaway ordering-table DMA — this, not input, is the current blocker

## Evidence

Exit code 139/134 with the framework's fail-fast: '[rq:error] FATAL: render queue full (65536 items) — refusing to drop prims (fail-fast). A submit path produced > 65536 prims this frame (runaway re-submission?)'. Call chain: main gen_func_80012204 -> gen_func_8001ED5C -> gen_func_8001E6B8 -> gen_func_8005FD64 -> gen_func_80061820 -> Core::io_write -> GpuState::gpu_dma2_linked_list -> gpu_gp0. A linked-list DMA that produces >65536 prims in one frame means the OT is malformed or unterminated, so the walk does not end. Frame-bound not time-bound: 20s and 70s timeouts both yield exactly 3781 frames. Context from the frame dump: the guest submits ZERO prims for the whole run before this (the SCE and Universal logos are VRAM uploads via dma2, not geometry), content ends with a deliberate fade at frame 434, and 3346 black frames follow while the guest keeps flipping buffers (disp alternating (0,0)/(0,240), 13 gp0words/frame). So the crash is the game finally entering its real OT-based render path.

## What would falsify it

A run that passes frame 3781, or a frame count that changes with the timeout — either would mean the crash is not deterministic at that point.

## FALSIFIED 2026-07-28

Both halves are now wrong. (1) MECHANISM: I called it 'a runaway ordering-table DMA' walking a 'malformed or unterminated' OT. The OT was fine — the framework's own 'ot' diagnostic never fired (it only reports a chain that fails to terminate), and a scene dump of frame 3779 shows a healthy 449 polys. The real mechanism was that nothing ever DRAINED the render queue, so it accumulated across frames until it hit RQ_MAX (C037). I reached for 'the data must be corrupt' when the answer was 'the consumer is missing'. (2) STATUS: fixed — flushing the queue in the vblank wait removed the abort and the run advances 3781 -> 3931 frames. Its one surviving correct part, that this outranked input as the blocker, is preserved in the frontier. See C037 for the cause and C038 for what it did NOT fix.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
