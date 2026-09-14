---
id: 113
title: Attract demo aborts: secondary/shaded producers refuse their combined atomic recipe
status: open
created: 2026-09-14
updated: 2026-09-14
---

## Symptom

A run that lets the attract demo play aborts:

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 0
  (secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8
  stage=0/3/2 load_stage=4294967295 state_switch=0
[watchdog] FAULT (signal): backtrace:
  SpyroRenderer::abortUnimplemented -> renderScene -> drawFrame -> Spyro1FrameDriver::stepFrame
```

Immediately before it, the paired-actor ownership gate was PASSing with 177-179 faces, i.e. the demo is
a real rendered gameplay frame and the refusal is in the composition step.

## Route and why nobody has seen it

`scratch/logs/intro_skip_no-hold.log`: the port driven from boot with NO input (`tools/drive.py`'s
`--hold/--tap/--after` are all gated on arrival in GS_Playing, so they cannot affect this window).
The drive's normal route taps Start at the title, which skips the attract demo, so every ordinary
driven run reaches gameplay without ever entering this state.

## Status: NOT YET DETERMINISTIC

A second run over the same field range did NOT reproduce it: the only difference was the screenshot
cadence, so the abort is sensitive to when the host syncs. Until a deterministic reproduction exists
this is an observation, not a fixable defect: the inner refusal reason was not captured either (the
first run had no debug channels, and the non-reproducing run produced no REFUSED line). Next step is
`PSXPORT_DEBUG=fieldactors,fieldshaded` on a route that reproduces it reliably, which names the
refusing plan and its admission inputs.

## Not the same as issue 0103

0103 is the dragon cutscene's shaded producer refusing a single un-implemented variant; that is fixed.
This is the secondary/shaded COMBINED atomic recipe refusing in the attract demo, at stage 0/3/2.
