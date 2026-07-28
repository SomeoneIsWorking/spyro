---
id: 18
title: No headless way to see what the VK renderer produced — a per-frame readback hangs the port
status: open
symptom: PSXPORT_GPU_DUMP cannot show rasterised geometry (I008), and the only VK readback path (gpu_vk_shot_region) is reachable solely from the interactive REPL — unusable from a batch run or a gate.
tags: gpu,tooling,dead-end
created: 2026-07-28
updated: 2026-07-29
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


## ATTEMPT 3, ALSO REVERTED — three call sites, three distinct failures (C061)

Called gpu_native_shot from the port's OWN vblank handler, AFTER gpu_present returns, on the theory
that the earlier hangs were caused by a command buffer still being in flight. Result: SIGSEGV right
after renderer init when armed early, and no fire at all when armed for a later frame.

So the three attempts fail in three different ways — hang, hang, segfault — from three different call
sites, including the framework's own chosen one. That is enough to stop treating this as a placement
problem. The transfer buffer is created during 3D init (the log confirms it), so it is not a missing-
buffer lifecycle bug either. Something in readback_vram's sequence
(AcquireGPUCommandBuffer -> copy pass -> DownloadFromGPUTexture -> SubmitAndAcquireFence ->
WaitForGPUFences -> MapGPUTransferBuffer) does not work under PSXPORT_VK_HEADLESS here.

And the REPL's working `shot` is NOT counter-evidence: repl.read() is only called from native_boot's
scheduler, which this port never runs, so that path has likely never executed headless in this
consumer at all.

STOPPING HERE deliberately. Three reverted attempts is the point at which more variations are waste;
this needs someone to debug the VK backend's headless readback directly, with a Vulkan validation
layer enabled, which is a different kind of task from porting a game. The port itself is unaffected —
this only blocks answering "are the pixels CORRECT", which remains genuinely unanswered.

WHAT STILL WORKS for visual checking, and should be used instead until this is fixed:
  * a WINDOWED run (PSXPORT_VK_WINDOW=1) — a human can simply look at it;
  * prim submission counts from PSXPORT_DEBUG=gpu, which prove the guest is DRAWING (gate check 4)
    but say nothing about pixels;
  * PSXPORT_GPU_DUMP for anything that reaches s_vram — uploads and fills only, never rasterised
    geometry (I008).

### Note (2026-07-29)
REOPENED AND LARGELY FALSIFIED — the recorded diagnosis was wrong. It never hung.

THE REAL FAULT: gpu_vk.cpp called hooks->renderFadeState UNCONDITIONALLY at five sites. That hook is OPTIONAL — two other sites in the same file already null-check it, which is what makes it optional by design — and Spyro leaves it nullptr (game_hooks.cpp line 81). So every shot/dump path segfaulted immediately. Port exit status is 139, not a hang.

WHY IT READ AS A HANG, and this is the part worth keeping: a crash early in the run produces 'exactly ONE frame in 25s instead of 3931, and writes no files', which looks identical to blocking unless you check the EXIT STATUS. This project already recorded that exact trap once — the gate reported PASS on a segfaulting port for its entire existence because 'timeout -s KILL' swallows the child's status. Same mistake, different tool, four months apart. Checking rc was a one-command test that was never run.

The three earlier hypotheses were all reasonable and all wrong: placement (falsified twice), image_write_rgb24 (ruled out), and 'the readback blocks' (falsified now — a step-trace through readback_vram shows enter -> targets ok -> cmd acquired -> submitted -> FENCE SIGNALLED, every time).

FIXED in psxport: one null-safe accessor fade_state_of() routes every call site, absent meaning 'no fade' which is what the two guarded sites already did.

HEADLESS CAPTURE NOW WORKS. 'preseq 6' from the REPL writes six 512x240 PPMs, correct resolution per C068, clean exit. The REPL itself only became reachable earlier this session (it is pumped from vsync.cpp's frame boundary), so this path was doubly blocked.

WHAT REMAINS, stated as an instrument caveat rather than a conclusion: every captured frame is UNIFORMLY BLACK (1 distinct colour) at both frame 900 and frame 4000, which is precisely the broken-instrument tell. Ruled out already: the fade (fade_mode 0 takes neither branch in dump_to, so my null-safe default is a genuine no-op), the region (s_last_w/h are 512x240, correct), and 'blank moment' (two widely separated capture points). So the content is not in s_vram_tex at readback time. Next: establish where it IS — the present-source selector s_present_ires picks between s_vram_tex and the s_ires_color composite, and the CPU-side s_vram demonstrably varies (gate reports 21 distinct frame occupancies) while this GPU texture reads black.

### Reopened (2026-07-29)
reopened
