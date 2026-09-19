---
id: 115
title: The first gameplay frame stalls about three seconds decoding CD audio, tripping the frame watchdog
status: open
symptom: every unattended run aborts at the gameplay boundary with "[watchdog] STUCK: no frame presented within the timeout" because the first product step spends about 3.0 s inside cd_codec_decompress reading CD audio out of the cold CHD
state_items: S011, S020
tags: performance, disc, audio, watchdog, startup
created: 2026-09-19
updated: 2026-09-19
---

## What happens

The step that first reaches the gameplay frame boundary does not present for about three seconds.
The framework's frame-progress watchdog defaults to three seconds, so it fires, prints its backtrace
and aborts the process. The backtrace names the whole path:

```
ecc_generate -> cd_codec_decompress -> cdlz_codec_decompress -> disc_read_raw
  -> CDC_GetCDAudioSample -> SPU_UpdateFromCDC -> SpuAudio::frameEx
  -> spyro1::FieldScheduler::deliver -> spyro1::deliverNativeField
  -> nativeFrameEnd -> SpyroRenderer::drawFrame -> spyro1::Spyro1FrameDriver::stepFrame
```

So this is CD audio being pulled through the CHD codec on the field the renderer delivers, with the
hunk cache cold. It is one stall, on one step, not a recurring cost: the frame-time distribution
over the following 3,360 steps has p99 3.75 ms in 4:3 and 7.25 ms at interpolated 60fps.

## Why it matters more than one slow frame

It makes every unattended long run abort. Two `looks_right.py` runs at 7,200 fields were reported as
"no capture from the 4:3 run" before the cause was found, because the process died at the boundary
and never reached the shot frame. Any agent measurement, any CI-style run, and any player whose
first level load is slower than this machine's meets the same abort. Raising `PSXPORT_WATCHDOG` hides
it; it does not fix it.

## What is measured

| run | p50 | p95 | p99 | worst | frames past the 128 ms range |
|---|---|---|---|---|---|
| 4:3 | 2.25 ms | 3.00 ms | 3.75 ms | 3039.52 ms | 1 |
| 16:9 | 2.50 ms | 3.25 ms | 4.00 ms | 3064.03 ms | 1 |
| interpolated 60fps | 5.25 ms | 6.25 ms | 7.25 ms | 3123.67 ms | 1 |

Local Linux x86-64 Clang build, offscreen and unpaced, 3,360 product steps of
`replays/gameplay/artisans-arrival.pad`, `PSXPORT_DEBUG=perf`. Exactly one frame per run lands past
the range, and it is this one.

## What would resolve it

Not a longer watchdog and not a smaller decode. The CD audio read happens synchronously on the
field the renderer is delivering, which is the wrong owner for blocking disc I/O. Either the CHD
hunks this step needs are resident before the gameplay boundary is crossed, or the CD audio sample
path stops being able to block a field delivery at all. Deciding which requires knowing whether the
guest expects audio to be available on that exact field; that is not yet established.
