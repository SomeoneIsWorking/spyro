---
id: I037
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

PSXPORT_DEBUG=presentskip (gpu_vk.cpp present())

## Validated by

Tallies the PresentRebuild decision with its denominator, ONE line per present (the channel is silent by default and the emit is an unguarded lucent::debug, so the gate lives in the logger, not in an `if`): 'presents=N reuse_last=A rebuild_geom=B rebuild_vram=C | vram_writes=W'. It can produce every one of its three answers and does so in a single Spyro run — over 218462 presents it reads reuse_last=404, rebuild_vram=33, rebuild_geom=218025, and the first two plateau by present ~600, so the boot-logo phase is separable from the 3D scene by these counters alone. So it is not a counter that only ever moves one way, and A+B+C is printed against the true present count so a zero in any column is distinguishable from 'never looked'. This is the instrument to run on Tomba2Engine and spider1 before landing the present-policy change: it answers 'does this port's guest write VRAM on an empty-batch present' directly, in one run, without a rebuild.

## Known failure modes

(none recorded yet)
