---
id: 100
title: Current field audio cadence lacked a machine-readable runtime trace
status: resolved
symptom: audio evidence had wall-clock/WAV totals but no per-field expected-versus-rendered record
tags: spyro1,audio,timing,field,diagnostics
created: 2026-08-29
updated: 2026-08-29
---

Affected state items: S004, S005.

## Root cause

The scheduler and SPU field cadence already computed exact fractional samples and clocks, but the
runtime only exposed aggregate WAV duration and pacing lines. Those aggregates could not distinguish
a correct 735/736 field schedule from a buffered or dropped output path, and they did not carry the
current build's field ordinal or PCM activity.

## Fix

The framework `SpuAudio::frameEx` path now emits an opt-in `audiofield` record for every advanced
field. Each record includes expected clocks/samples, rendered and queued samples, a deterministic PCM
summary, output mode, and XA state/deltas. The instrumentation does not change scheduling, resampling,
or XA policy; it makes the existing path falsifiable.

## Verification

The current Clang Spyro build ran 1,200 NTSC fields with 1,200 `audiofield` records. Every field
advanced and rendered exactly 735 or 736 samples; the expected and queued totals were both 882,882.
The captured file is valid 44.1 kHz stereo 16-bit PCM, 3,531,572 bytes including its header, and the
records are non-silent. This proves current field cadence and sink activity, not PCM equality against
the retained/reference leg; clock alignment and a reference audio comparator remain future work.

## SBS boundary confirmed (2026-08-29)

A 120-field SBS run using the current build reached the dual-core lockstep path, but diverged at
the boot/load boundary before FIELD. The `audiofield` records from the two cores are interleaved
without a core identity or scheduler-frame identity, and the run has no comparator that can pair
their PCM summaries. It is therefore evidence that the existing capture is reachable in SBS, not an
oracle speed/audio verdict. The remaining work is a framework-owned per-core field report and
comparator; no audio timing policy was changed here.
