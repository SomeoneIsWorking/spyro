---
id: 15
title: Port aborts at frame 3781: render queue full (65536 prims) from a runaway OT linked-list DMA
status: open
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
