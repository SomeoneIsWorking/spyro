---
id: 18
title: No headless way to see what the VK renderer produced — a per-frame readback hangs the port
status: dead-end
symptom: PSXPORT_GPU_DUMP cannot show rasterised geometry (I008), and the only VK readback path (gpu_vk_shot_region) is reachable solely from the interactive REPL — unusable from a batch run or a gate.
tags: gpu,tooling,dead-end
created: 2026-07-28
updated: 2026-07-28
---

ATTEMPTED AND REVERTED. I added a PSXPORT_VK_DUMP=dir[:every] block to gpu_present_ex mirroring
PSXPORT_GPU_DUMP but calling gpu_vk_shot_region. It does not work: with the variable set the port
produces exactly ONE frame in 25s instead of 3931, and writes no files. So the readback BLOCKS on the
first call — it never returns and never logs its own 'wrote' line. vk_path() is confirmed 1 at that
point, so the guard is not the issue.

Reverted rather than shipped. A diagnostic that hangs the program under test is worse than none, and I
do not understand the cause well enough to leave it in — most likely a GPU fence/readback that cannot
complete at that point in the frame, or one that requires a submit that headless mode has not made.

WHY THIS MATTERS: it leaves a real hole. 'Are the pixels correct' is currently unanswerable in a batch
run. The prim-submission count (now gate check 4) proves the guest is DRAWING, which is a different and
weaker claim.

ATTEMPT 2, ALSO REVERTED — but it isolates the fault (C045).

The framework ALREADY has what I was building: gpu_vk_preseq_arm(), "dump the next N PRESENTED frames",
documented as working windowed AND headless. It is reachable only from the interactive REPL. I exposed it
as PSXPORT_PRESEQ="N[:dir][@startFrame]" at its own dump site inside GpuVkState::frame_end — which is the
framework's chosen placement, AFTER the present-pass readback, i.e. exactly where hypothesis (2) below said
the problem was.

Arming works ("[preseq] env-armed at frame 0: 3 present passes"). Zero files are written and the run
produces 1 frame in 25s instead of 3931 — the IDENTICAL hang to attempt 1 at a completely different call
site.

So: dump_to / readback_vram BLOCKS wherever it is called from. Hypothesis (2) is FALSIFIED — placement is
not the issue. image_write_rgb24 is ruled out too: for a .ppm path it is a plain fopen/fwrite with no
encoder dependency.

NEXT, narrowed to one function: instrument readback_vram itself (the SDL_GPU download + fence wait +
SDL_MapGPUTransferBuffer). The likely candidates are a fence that never signals because no command buffer
has been submitted in this consumer's headless path, or a transfer buffer that is never created outside
the windowed present. Note the REPL path is NOT proof the mechanism works here — the REPL is only reachable
in an interactive session, which is windowed, so it may never have run headless in this consumer at all.
Do not treat "documented as headless-safe" as evidence; it was not evidence.
