---
id: C045
kind: claim
status: holds
created: 2026-07-28
tags: gpu,tooling
---

## Claim

The VK readback itself blocks in this port's headless config — not a placement problem, and the framework's own preseq capture hangs too

## Evidence

Second independent attempt at headless capture of renderer output, and it isolates the fault. The framework already ships gpu_vk_preseq_arm() — 'dump the next N PRESENTED frames', documented as working windowed AND headless — but it is reachable ONLY from the interactive REPL. I exposed it via PSXPORT_PRESEQ='N[:dir][@startFrame]' at its own dump site inside GpuVkState::frame_end, which is the framework's chosen placement (after the present-pass readback, exactly where I had hypothesised my earlier attempt went wrong). Arming works — '[preseq] env-armed at frame 0: 3 present passes' logs — but ZERO files are written and the run produces 1 frame in 25s instead of 3931. Identical hang to the earlier PSXPORT_VK_DUMP attempt at a different call site. So dump_to / readback_vram BLOCKS wherever it is called from, and my earlier 'wrong placement / incomplete submit' hypothesis is FALSIFIED. image_write_rgb24 is not the cause: it handles a .ppm path with a plain fopen/fwrite and no encoder dependency. The fault is in readback_vram or the transfer-buffer map it does. Both attempts reverted; psxport tree is clean.

## What would falsify it

A headless run in which readback_vram returns and a file appears — which would mean the block is configuration-specific rather than inherent to this path.
