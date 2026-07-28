---
id: C061
kind: claim
status: holds
created: 2026-07-28
tags: gpu,tooling
---

## Claim

Headless VK readback fails in three distinct ways from three different call sites — it is a framework-side limitation, not a placement problem

## Evidence

Three attempts, all reverted, all measured. (1) A per-frame PSXPORT_VK_DUMP inside gpu_present_ex: HANGS — 1 frame in 25s instead of thousands, no file. (2) Env-arming the framework's OWN preseq capture at its own dump site in GpuVkState::frame_end, i.e. the placement the framework itself chose: also HANGS, identically, which falsified my hypothesis that placement was the problem. (3) Calling gpu_native_shot from the port's vblank handler AFTER gpu_present has returned, when no command buffer should be in flight: SIGSEGV (signal 11) right after renderer init when fired early, and simply never fires when armed for a later frame. The transfer buffer is created during 3D init, which the log confirms happened, so this is not a missing-buffer lifecycle bug. readback_vram does AcquireGPUCommandBuffer -> copy pass -> DownloadFromGPUTexture -> SubmitAndAcquireFence -> WaitForGPUFences -> MapGPUTransferBuffer; something in that sequence does not work under PSXPORT_VK_HEADLESS in this consumer. NOTE the REPL's  is not counter-evidence: repl.read() is only called from native_boot's scheduler, which this port never runs, so that path has probably never executed headless here at all.

## What would falsify it

A headless run in which any of the three call sites produces a file — which would make this configuration-specific rather than inherent.
