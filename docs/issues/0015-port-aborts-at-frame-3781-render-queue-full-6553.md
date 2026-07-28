---
id: 15
title: Port aborts at frame 3781: render queue full (65536 prims) from a runaway OT linked-list DMA
status: resolved
symptom: [rq:error] FATAL: render queue full (65536 items) — refusing to drop prims (fail-fast), then SIGABRT/SIGSEGV. Exit 139. Always at frame 3781.
tags: gpu,blocker,ot
created: 2026-07-28
updated: 2026-07-28
---

See claim C036. Deterministic and frame-bound, not time-bound (20s and 70s timeouts both give exactly
3781 frames).

Chain: main 0x80012204 -> 0x8001ED5C -> 0x8001E6B8 -> 0x8005FD64 -> 0x80061820 -> io_write ->
GpuState::gpu_dma2_linked_list -> gpu_gp0.

0x80061820 is the DMA2/GPU submit (DrawOTag-equivalent). >65536 prims from one linked-list walk means
the ordering table is malformed or unterminated — the walk follows garbage 'next' pointers instead of
hitting the 0xFFFFFF terminator.

Leading hypotheses, none tested yet:
  1. The OT was never cleared (ClearOTagR equivalent never ran, or ran on the wrong address), so it
     contains whatever was in that memory.
  2. The OT base the guest hands to DMA is wrong.
  3. A real Spyro OT is being walked correctly but the framework's prim budget is genuinely too small
     — LEAST likely and must not be assumed, because raising the cap to make the symptom vanish is
     precisely the bandaid the project rules forbid. The cap is a fail-fast, not the bug.

WHAT THE FRAME DUMP SAYS ABOUT WHEN. The guest submits ZERO prims for the entire run up to this point
— the SCE and Universal logos are VRAM uploads (dma2), not geometry. Content ends with a deliberate
fade at frame 434, then 3346 black frames while the guest keeps flipping buffers normally
(disp alternating (0,0)/(0,240), 13 gp0words/frame). So the crash is the moment the game first enters
its real OT-based render path, and the black period before it is the game running its main loop
without drawing.

Note also: the logo screens that DO render show heavy colour speckling (solid white text renders as
rainbow noise) and are horizontally truncated. Tracked separately — do not conflate with this.

### Resolution (2026-07-28)
ROOT-CAUSED and the abort is FIXED; the black screen it was bundled with is NOT (see below).

CAUSE (C037): nothing drained the render queue. The guest's DrawOTag walks its OT and QUEUES prims
(gpu_dma2_linked_list -> gpu_gp0 -> rq.push), but rq.flush()/emitQueue() are only reached from the
framework's native_boot / Engine::drawOTag path, which this port never runs — the guest owns its own
frame loop. RenderQueue::push() resets lazily and only when 'consumed' is set, which only flush sets.
With no consumer the queue grew ~449 polys/frame until it hit RQ_MAX 65536 about 146 drawing-frames
later, and the framework fail-fasted exactly as designed.

psxport's native_boot.cpp documents this same bug from its own history, with the same black-front-end
symptom. Worth reading that comment before diagnosing anything else in this area.

FIX: game/core/vsync.cpp calls c->game->rq.flush(c) before gpu_present in the vblank wait — the same
place, and for the same reason, as the per-frame BIOS event delivery already there: that wait is this
port's real per-frame boundary because native_step_frame never runs.

The 65536 cap was NOT raised. It is the fail-fast, not the bug.

VERIFIED: the rq:error abort no longer fires; the run advances 3781 -> 3931 frames and now stops on a
different, further-along failure (a recomp miss at 0x8008772C, tracked separately).

STILL BROKEN, and my single-cause prediction was wrong (C038): the screen is still black after frame
434. Prims now reach the renderer but not the screen. First thing to check — untested — is that the
framework's native path pairs its draw with gpu_set_disp_origin(c,0,0) so 'present scans the page we
draw', while Spyro's guest double-buffers with disp alternating (0,0)/(0,240).
