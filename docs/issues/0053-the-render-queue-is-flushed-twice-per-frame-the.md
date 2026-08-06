---
id: 53
title: The render queue is flushed twice per frame: the per-vblank flush in vsync.cpp re-emits an already-consumed queue
status: open
symptom: PSXPORT_DEBUG=rqflush reports reemit=1 on ~70% of flushes; every prim the guest drew is submitted to the VK geometry batch more than once per displayed frame
tags: render,render-queue,vsync,double-submit,measured
created: 2026-08-06
updated: 2026-08-06
---

## What was measured

Build: my render-seam change + HEAD `game/core/vsync.cpp`, clean Release dir, headless,
`PSXPORT_NOPACE=1 PSXPORT_DEBUG=rqflush,pace`, 15 s boot (`scratch/logs/nativerender/rqflush2.log`):

    flushes = 7936      reemit=1 on 5527 of them (69.6%)
    [pace] vbl=3990 ... rq_unconsumed=0        <- over 3990 vblanks, NEVER the first consumer

`RenderQueue::flush` marks the queue consumed but only RESETS it on the next `push` after that
(render_queue.cpp `reset()` / `mark_consumed()`), so flushing a consumed non-empty queue walks the
same items through `emitItem` again. `reemit=1` on the `rqflush` line is exactly that state.

## Cause (named, not guessed)

TWO call sites drain the queue on this port and only one of them is needed:

1. `GpuState::gpu_dma2_linked_list` (framework, gpu_native.cpp) flushes at the END of the guest's own
   DrawOTag DMA walk. Its comment says it was added FOR the guest-driven path — i.e. for exactly this
   port's shape.
2. `game/core/vsync.cpp` flushes once per VBLANK, in the vblank wait. Its comment still says
   "rq.flush() is only reached from the framework's native_boot / Engine::drawOTag path, which this
   port never runs" — that was true when it was written and is now STALE: (1) covers it.

Spyro draws ~1 frame per 1.86 vblanks, so each drawn frame's queue is re-emitted 1-3 extra times.

`rq_unconsumed=0` is the denominator that makes this a fix rather than a guess: the instrument in
vsync.cpp exists precisely to detect "this loop was the queue's first consumer", and over 3990
vblanks it never was. If some path ever queues prims OUTSIDE the OT walk (a direct GP0 producer, an
FMV path), that counter goes non-zero and the vsync flush would be load-bearing — so the fix is
"delete the vsync flush and watch that counter stay 0", not "delete it and hope".

## Why it was not fixed in the same step

Found while standing up the native render seam (which deliberately does NOT add a third flush site).
Removing the vsync flush changes the shipping picture path, so it wants its own before/after with a
picture check, and that step's gate was "psx_render behaves exactly as today". BLIND SPOT of the
measurement above: boot + attract + the field demo only; no FMV, no menu-heavy screen.
