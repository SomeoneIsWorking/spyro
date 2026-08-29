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
field. Each record includes expected clocks/samples, rendered and queued samples, the exact stereo PCM
samples, a deterministic PCM summary, output mode, and XA state/deltas. SBS owns a separate comparator
that pairs those reports after each lockstep frame. The instrumentation and comparator do not change
scheduling, resampling, or XA policy; they make the existing path falsifiable.

## Verification

The current Clang Spyro build ran 1,200 NTSC fields with 1,200 `audiofield` records. Every field
advanced and rendered exactly 735 or 736 samples; the expected and queued totals were both 882,882.
The captured file is valid 44.1 kHz stereo 16-bit PCM, 3,531,572 bytes including its header, and the
records are non-silent. This proves current field cadence and sink activity. The later real SBS oracle
run reached 120 lockstep frames, emitted 240 paired `audiofield` records, and produced no
`[sbs-audio:error]`; the control run with identical game logic also matched on every observed field.

## SBS boundary confirmed (2026-08-29)

A 120-field SBS run using the current build reached the dual-core lockstep path. Before the fix, the
per-core SPU output ring was process-global and SBS did not rebind the active SPU before each core
step, so even the same-logic control run produced different A/B PCM fields. The fix moves the ring and
cursor into each Beetle `SpuState` and rebinds the owning `SpuDevice` at every core step. The oracle
run now pairs exact per-field PCM reports and shows no audio mismatch for all 120 frames. The run
still has known non-audio boot/state divergence, so this is an audio-field parity result, not a claim
of complete oracle, visual, or transition parity; no audio timing policy was changed.
