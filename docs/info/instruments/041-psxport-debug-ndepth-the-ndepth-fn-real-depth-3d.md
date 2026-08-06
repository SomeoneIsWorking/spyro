---
id: I041
kind: instrument
status: DISTRUSTED
created: 2026-08-06
distrusted_on: 2026-08-06
---

## Instrument

PSXPORT_DEBUG=ndepth — the '[ndepth fN] real-depth(3D) prims=... 3D%=...' coverage line (external/psxport/runtime/recomp/gpu_native.cpp:1439)

## Validated by

REGISTERED ALREADY DISTRUSTED 2026-08-06 — this entry exists to record a tool caught lying, so it was never validated in the sense this field asks for. See the DISTRUSTED section below for the mechanism, the denominator problem, and what it would take to trust it.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-06

IT PRINTS A ONE-FRAME SAMPLE FORMATTED AS A COVERAGE MEASUREMENT, AND PRINTS THE SAME "0.0" WHEN IT HAS NO DATA AT ALL.

MECHANISM, read out of THIS tree (spyro external/psxport @ dbc5a5e1, gpu_native.cpp):
  * line 1439  the report is gated `if (s_frame > 0 && (s_frame % 60) == 0)`
  * line 1466  `core->rsub.stats.nd3d = core->rsub.stats.nd2d = 0;` runs on EVERY present, unconditionally
  * line 1454  `core->rsub.projprim.statsReset()` likewise — the projprim records/hit/miss line has
               exactly the same shape
So the counters cover ONE frame, not the 60 between reports, and the line never says so — it prints
`[ndepth f600] real-depth(3D) prims=... OT-band(2D) prims=... 3D%=...` with no denominator and no
statement of the window it covers. 59 of every 60 frames are discarded unread.

AND THE ZERO CASE IS INDISTINGUISHABLE FROM THE NO-DATA CASE. The percentage is
`(nd3d+nd2d) ? 100.0*nd3d/(nd3d+nd2d) : 0.0`, so a frame in which NOTHING WAS COUNTED prints
`3D%=0.0` — byte-for-byte identical to a frame that genuinely drew 0% 3D. That is the "a diagnostic
that can print nothing is lying" failure exactly: silence was given a number.

MEASURED ON THE SIBLING PORT (spider1, same framework code): Spider-Man draws on alternate fields, 60
is even, so every `s_frame % 60 == 0` sample landed on a NON-DRAWING field and the channel printed
`3D%=0.0` for an entire run. It read as "the native depth path covers nothing". It was measuring
nothing. That aliasing is a property of the SAMPLING PARITY, so it can hit any port whose drawing
cadence divides into 60. Whether it hits THIS port is answered in the next paragraph — from existing
evidence, not from a run made here.

THE ALIASING HALF IS NOT KNOWN TO AFFECT SPYRO, AND THERE IS EVIDENCE IT DOES NOT — stated because
the spider1 failure is dramatic and the temptation is to assume it here. C145's own evidence reads
"per sampled frame the port alternates hit=1547/miss=0 and hit=0/miss=1540". CONSECUTIVE ndepth
samples in this port land on BOTH phases of a 2-frame alternation. If every sample sat at one fixed
parity that alternation could not have appeared in the log at all, so spyro's samples demonstrably
span both phases and spyro's ndepth output is NOT phase-locked the way spider1's was. Do not carry
spider1's "3D%=0.0 for a whole run" over to this port — it has not happened here.

WHAT DOES APPLY HERE, unconditionally, because it is structural: the missing denominator and the
zero/no-data collision above. Both are in the code in THIS tree, independent of any cadence.

RESULTS IN THIS REPO THAT REST ON IT — re-read the caveat before citing, do NOT treat as refuted:
C128 and C145 (native per-vertex depth), and issues 0036, 0038 and 0041. (C125 is already `falsified`
for an unrelated and correctly-diagnosed reason — it sampled a frame chosen BECAUSE ndepth said it
had 3D prims — and needs nothing from this entry.) Every projprim `records/hit/miss` and every `3D%`
figure in those is a SINGLE-FRAME snapshot, whatever the number of samples aggregated; C128 is
explicit that its figures are over "1572 sampled frames", which is the honest form. Nothing here says
those conclusions are wrong. It says the per-line window is one frame and is never printed, so any
future line quoted from this channel must carry that qualification.

NOT RECONCILED, and left open rather than guessed: C128's "1572 sampled frames" over a 130 s run does
not divide cleanly by a 60-frame sampling period at any obvious frame rate. Either `s_frame` advances
faster than the displayed frame in this port, or that run's build reported more often. Anyone
re-deriving these figures should settle that first — it changes what the aggregate denominators mean.

TO TRUST IT AGAIN, both of these — neither is optional:
  1. CARRY THE DENOMINATOR. Accumulate across the whole interval (or state "1 frame" in the line),
     and print the frame count and the raw totals, so "0 of 0" can never be rendered as a percentage.
     A no-data sample must print something a reader cannot mistake for a measurement — e.g.
     `3D%=n/a (0 prims counted in 1 frame)`.
  2. SAMPLE BY DRAW EVENT, NOT BY FRAME PARITY. Report on the Nth frame THAT DREW ANYTHING, not on
     every 60th frame index, so an alternate-field or otherwise periodic cadence cannot alias the
     sample to a silent field.
Until both land, do not read this channel as coverage at all. Its `OVERFLOWED — records were DROPPED`
flag is still worth having: that one is a real event, not a rate.

> Every result this instrument produced is suspect until it is re-validated.
