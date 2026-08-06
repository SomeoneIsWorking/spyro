---
id: I044
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

PSXPORT_DEBUG=rqflush (render_queue.cpp RenderQueue::flush) — one line per flush: n= items, reemit= (the queue was already consumed, so this flush re-sends it), seq=, and y=[lo..hi], the min/max ys[] of the queued items. The y-range is what identifies WHICH FRAMEBUFFER a queue was drawn into, because ys[] carries the guest's draw offset.

## Validated by

Run against BOTH classes on Spyro, windowed, presents ~2646..2653: the SAME queue reports two different y bands 240 apart (y=[-93..332] and y=[147..572]) as the guest alternates buffers, and reemit flips 0/1 within one guest frame (fresh flush then two re-emits). Negative side: with the deferred reset removed the reemit bit read 0 on every flush and the picture went permanently flat — the instrument tracked the change rather than staying uniform. Blind spots: it reports the QUEUE, not what the rasterizer accepted (scissor/clip are downstream), and n=0 prints y=[0..0] rather than a range, so an empty queue must be read off n, not off y.

## Known failure modes

(none recorded yet)
