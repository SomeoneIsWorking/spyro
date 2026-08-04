---
id: I037
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

PSXPORT_DEBUG=presentskip (gpu_vk.cpp present())

## Validated by

Tallies the PresentRebuild decision with its denominator every 200 presents: 'presents=N reuse_last=A rebuild_geom=B rebuild_vram=C | vram_writes=W'. It can produce every one of its three answers and does so in a single Spyro run — reuse_last 192/200 in the first 200 presents (the boot logos, where the guest uploads and re-presents), rebuild_vram 33 total, then rebuild_geom for the remaining 83963 of 84400 once the 3D scene starts. So it is not a counter that only ever moves one way, and A+B+C is printed against the true present count so a zero in any column is distinguishable from 'never looked'. This is the instrument to run on Tomba2Engine and spider1 before landing the present-policy change: it answers 'does this port's guest write VRAM on an empty-batch present' directly, in one run, without a rebuild.

## Known failure modes

(none recorded yet)
